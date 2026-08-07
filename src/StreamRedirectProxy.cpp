/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "StreamRedirectProxy.h"
#include "Base64.h"
#include "Crypto.h"
#include "Utils.h"
#include "http/Curl.h"
#include "rapidjson/document.h"

#include <kodi/General.h>

#include <cstring>
#include <sstream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
constexpr uint8_t kAesBlockSize = 16;
// Matches PLATFORM_ANDROIDTV in PVREon.h. Kept as a local literal to avoid
// pulling in the full PVR client header from this standalone proxy.
constexpr int kAndroidTvPlatform = 1;
// How long a redirect for the same seek timestamp is reused instead of
// minting a fresh encrypted URL, to absorb ffmpegdirect's retry requests.
constexpr auto kSeekCacheWindow = std::chrono::milliseconds(10000);

// Mirrors PLAYER/CONN_TYPE_* in Globals.h. Not reused directly because
// Globals.h's EON_USER_AGENT needs KODI_VERSION, which is only defined
// transitively via kodi/addon-instance/PVR.h (pulled in by PVREon.h, not
// by this standalone proxy).
const std::string kPlayer = "m3u8";
const std::string kConnTypeEthernet = "ETHERNET";
const std::string kConnTypeBrowser = "BROWSER";

// The CDN rejects an encrypted URL if its embedded ctime is more than
// ~20 seconds old, so this must be fetched fresh for every seek -- mirrors
// CPVREon::GetTime(), which the non-proxy path calls for every request.
std::string FetchServerTime(const StreamParams& params)
{
  const int64_t fallbackMs = static_cast<int64_t>(time(nullptr)) * 1000;
  if (params.apiTimeUrl.empty())
    return std::to_string(fallbackMs);

  Curl curl;
  curl.AddHeader("Content-Type", "application/json");
  if (!params.userAgent.empty())
    curl.AddHeader("User-Agent", params.userAgent);
  if (!params.accessToken.empty())
    curl.AddHeader("Authorization", "bearer " + params.accessToken);

  int statusCode = 0;
  const std::string body = curl.Get(params.apiTimeUrl, statusCode);

  rapidjson::Document doc;
  doc.Parse(body.c_str());
  if (doc.GetParseError() || statusCode != 200 || !doc.HasMember("time") || !doc["time"].IsString())
  {
    kodi::Log(ADDON_LOG_ERROR, "StreamRedirectProxy: failed to fetch server time (status=%d), falling back to device clock", statusCode);
    return std::to_string(fallbackMs);
  }

  return doc["time"].GetString();
}
}

StreamRedirectProxy::StreamRedirectProxy() {}

StreamRedirectProxy::~StreamRedirectProxy()
{
  Stop();
}

bool StreamRedirectProxy::Start()
{
  if (m_running)
    return true;

  m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (m_serverSocket < 0)
  {
    kodi::Log(ADDON_LOG_ERROR, "StreamRedirectProxy: failed to create socket");
    return false;
  }

  int opt = 1;
  setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0; // Let the OS pick a port

  if (bind(m_serverSocket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
  {
    kodi::Log(ADDON_LOG_ERROR, "StreamRedirectProxy: failed to bind");
    close(m_serverSocket);
    m_serverSocket = -1;
    return false;
  }

  socklen_t addrLen = sizeof(addr);
  getsockname(m_serverSocket, reinterpret_cast<struct sockaddr*>(&addr), &addrLen);
  m_port = ntohs(addr.sin_port);

  if (listen(m_serverSocket, 5) < 0)
  {
    kodi::Log(ADDON_LOG_ERROR, "StreamRedirectProxy: failed to listen");
    close(m_serverSocket);
    m_serverSocket = -1;
    return false;
  }

  m_running = true;
  m_thread = std::thread(&StreamRedirectProxy::ServerThread, this);

  kodi::Log(ADDON_LOG_INFO, "StreamRedirectProxy: listening on 127.0.0.1:%d", m_port);
  return true;
}

void StreamRedirectProxy::Stop()
{
  if (!m_running)
    return;

  m_running = false;

  if (m_serverSocket >= 0)
  {
    shutdown(m_serverSocket, SHUT_RDWR);
    close(m_serverSocket);
    m_serverSocket = -1;
  }

  if (m_thread.joinable())
    m_thread.join();

  kodi::Log(ADDON_LOG_INFO, "StreamRedirectProxy: stopped");
}

void StreamRedirectProxy::SetStreamParams(const StreamParams& params)
{
  std::lock_guard<std::mutex> lock(m_paramsMutex);
  m_params = params;
}

void StreamRedirectProxy::ServerThread()
{
  while (m_running)
  {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(m_serverSocket, &readfds);

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    int ret = select(m_serverSocket + 1, &readfds, nullptr, nullptr, &tv);
    if (ret <= 0)
      continue;

    int clientSocket = accept(m_serverSocket, nullptr, nullptr);
    if (clientSocket < 0)
      continue;

    char buf[2048];
    int bytesRead = static_cast<int>(recv(clientSocket, buf, sizeof(buf) - 1, 0));
    if (bytesRead <= 0)
    {
      close(clientSocket);
      continue;
    }
    buf[bytesRead] = '\0';

    // Parse the timestamp from "GET /stream?t=<timestamp> HTTP/..."
    std::string request(buf);
    time_t timestamp = 0;

    size_t tPos = request.find("t=");
    if (tPos != std::string::npos)
    {
      tPos += 2;
      size_t tEnd = request.find_first_of("& \r\n", tPos);
      std::string tStr = request.substr(tPos, tEnd - tPos);
      try { timestamp = static_cast<time_t>(std::stoll(tStr)); }
      catch (...) { timestamp = 0; }
    }

    if (timestamp <= 0)
    {
      std::string response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
      send(clientSocket, response.c_str(), response.size(), 0);
      close(clientSocket);
      continue;
    }

    // Reuse the same encrypted URL for retries of the same timestamp.
    std::string streamUrl;
    {
      std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
      const auto elapsed = std::chrono::steady_clock::now() - m_lastSeekTime;
      if (timestamp == m_lastSeekTimestamp && elapsed < kSeekCacheWindow && !m_lastStreamUrl.empty())
      {
        streamUrl = m_lastStreamUrl;
        kodi::Log(ADDON_LOG_DEBUG, "StreamRedirectProxy: cache hit for t=%lld",
                  static_cast<long long>(timestamp));
      }
    }

    if (streamUrl.empty())
    {
      // A fresh session id avoids the CDN rejecting the request because
      // the previous session is still considered "active" server-side.
      streamUrl = BuildEncryptedUrl(timestamp);

      {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        m_lastSeekTimestamp = timestamp;
        m_lastStreamUrl = streamUrl;
        m_lastSeekTime = std::chrono::steady_clock::now();
      }

      kodi::Log(ADDON_LOG_DEBUG, "StreamRedirectProxy: seek to t=%lld -> new encrypted URL",
                static_cast<long long>(timestamp));
    }

    // 302 redirect (rather than proxying the body) so FFmpeg resolves
    // relative HLS segment/variant URLs against the real CDN host.
    std::string response = "HTTP/1.1 302 Found\r\n"
                           "Location: " + streamUrl + "\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n\r\n";
    send(clientSocket, response.c_str(), response.size(), 0);
    close(clientSocket);
  }
}

namespace
{
std::string BuildEncryptedUrlForSession(const StreamParams& params, time_t timestamp,
                                         const std::string& ctime, const std::string& sessionId)
{
  std::string plain_aes;
  if (params.platform == kAndroidTvPlatform)
  {
    plain_aes = "channel=" + params.publishingPoint + ";"
                "stream=" + params.streamingProfile + ";"
                "sp=" + params.serviceProvider + ";"
                "u=" + params.streamUser + ";"
                "m=" + params.serverIp + ";"
                "device=" + params.deviceNumber + ";"
                "ctime=" + ctime + ";"
                "lang=eng;player=" + kPlayer + ";"
                "aa=" + (params.aaEnabled ? "true" : "false") + ";"
                "conn=" + kConnTypeEthernet + ";"
                "minvbr=100;"
                "ss=" + params.streamKey + ";"
                "session=" + sessionId + ";"
                "maxvbr=" + std::to_string(params.maxBitrate) +
                ";t=" + std::to_string(static_cast<long long>(timestamp)) + "000;";
  }
  else
  {
    // No "adaptive=true;" here: the proxy is only ever used for replay via
    // ffmpegdirect, which never sets that hint in the original request (see
    // use_adaptive_stream_hint in PVREon.cpp's BuildPlaybackUrl) -- including
    // it produces a payload the CDN doesn't expect and silently rejects.
    plain_aes = "channel=" + params.publishingPoint + ";"
                "stream=" + params.streamingProfile + ";"
                "sp=" + params.serviceProvider + ";"
                "u=" + params.streamUser + ";"
                "ss=" + params.streamKey + ";"
                "minvbr=100;"
                "sig=" + params.sig + ";"
                "session=" + sessionId + ";"
                "m=" + params.serverIp + ";"
                "device=" + params.deviceNumber + ";"
                "ctime=" + ctime + ";"
                "conn=" + kConnTypeBrowser + ";"
                "player=" + kPlayer + ";"
                "t=" + std::to_string(static_cast<long long>(timestamp)) + "000;"
                "aa=" + (params.aaEnabled ? "true" : "false");
  }

  const std::string key = base64_decode(urlsafedecode(params.streamKey));

  std::string iv_str;
  iv_str.reserve(kAesBlockSize);
  for (int i = 0; i < kAesBlockSize; i++)
    iv_str.push_back(static_cast<char>(rand() & 0xFF));

  const std::string enc_str = aes_encrypt_cbc(iv_str, key, plain_aes);

  std::string enc_url = "https://" + params.serverHostname +
                         "/stream?i=" + urlsafeencode(base64_encode(iv_str.c_str(), iv_str.length())) +
                         "&a=" + urlsafeencode(base64_encode(enc_str.c_str(), enc_str.length()));

  if (params.platform == kAndroidTvPlatform)
    enc_url += "&lang=eng";

  enc_url += "&sp=" + params.serviceProvider +
             "&u=" + params.streamUser +
             "&player=" + kPlayer +
             "&session=" + sessionId;

  if (params.platform != kAndroidTvPlatform)
    enc_url += "&sig=" + params.sig;

  return enc_url;
}
} // namespace

std::string StreamRedirectProxy::BuildEncryptedUrl(time_t timestamp)
{
  StreamParams params;
  {
    std::lock_guard<std::mutex> lock(m_paramsMutex);
    params = m_params;
  }

  const std::string ctime = FetchServerTime(params);
  const std::string sessionId = Utils::CreateUUID();
  const std::string enc_url = BuildEncryptedUrlForSession(params, timestamp, ctime, sessionId);

  kodi::Log(ADDON_LOG_DEBUG, "StreamRedirectProxy: t=%lld -> %s",
            static_cast<long long>(timestamp), enc_url.c_str());

  return enc_url;
}
