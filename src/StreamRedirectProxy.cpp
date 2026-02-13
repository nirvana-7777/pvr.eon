/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "StreamRedirectProxy.h"
#include "Base64.h"

#define CBC 1
#include "aes.hpp"
#include "pkcs7_padding.hpp"

#include <kodi/General.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <random>
#include <sstream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static const uint8_t PROXY_BLOCK_SIZE = 16;
static const int PROXY_PLATFORM_ANDROIDTV = 1;
static const std::string PROXY_PLAYER = "m3u8";
static const std::string PROXY_CONN_TYPE_ETHERNET = "ETHERNET";
static const std::string PROXY_CONN_TYPE_BROWSER = "BROWSER";

static std::string proxy_generate_uuid()
{
  static const char* hex = "0123456789abcdef";
  unsigned seed = static_cast<unsigned>(
      std::chrono::system_clock::now().time_since_epoch().count());
  std::mt19937 gen(seed);
  std::uniform_int_distribution<int> dis(0, 15);

  std::string uuid = "xxxxxxxx-xxxx-4xxx-8xxx-xxxxxxxxxxxx";
  for (char& c : uuid)
  {
    if (c == 'x')
      c = hex[dis(gen)];
  }
  return uuid;
}

static std::string proxy_urlsafeencode(const std::string& s)
{
  std::string t = s;
  std::replace(t.begin(), t.end(), '+', '-');
  std::replace(t.begin(), t.end(), '/', '_');
  t.erase(std::remove(t.begin(), t.end(), '='), t.end());
  return t;
}

static std::string proxy_urlsafedecode(const std::string& s)
{
  std::string t = s;
  std::replace(t.begin(), t.end(), '-', '+');
  std::replace(t.begin(), t.end(), '_', '/');
  return t;
}

static std::string proxy_aes_encrypt_cbc(const std::string& iv_str,
                                         const std::string& key,
                                         const std::string& plaintext)
{
  int dlen = static_cast<int>(plaintext.size());
  int klen = static_cast<int>(key.size());
  int dlenu = dlen + PROXY_BLOCK_SIZE - (dlen % PROXY_BLOCK_SIZE);

  uint8_t hexarray[dlenu];
  uint8_t kexarray[klen];
  uint8_t iv[klen];

  memset(hexarray, 0, dlenu);
  memset(kexarray, 0, klen);
  memset(iv, 0, klen);

  for (int i = 0; i < dlen; i++)
    hexarray[i] = static_cast<uint8_t>(plaintext[i]);
  for (int i = 0; i < klen; i++)
  {
    kexarray[i] = static_cast<uint8_t>(key[i]);
    iv[i] = static_cast<uint8_t>(iv_str[i]);
  }

  pkcs7_padding_pad_buffer(hexarray, dlen, sizeof(hexarray), PROXY_BLOCK_SIZE);

  struct AES_ctx ctx;
  AES_init_ctx_iv(&ctx, kexarray, iv);
  AES_CBC_encrypt_buffer(&ctx, hexarray, dlenu);

  std::ostringstream convert;
  for (int i = 0; i < dlenu; i++)
    convert << hexarray[i];

  return convert.str();
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
      timestamp = static_cast<time_t>(std::stoll(tStr));
    }

    if (timestamp <= 0)
    {
      std::string response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
      send(clientSocket, response.c_str(), response.size(), 0);
      close(clientSocket);
      continue;
    }

    // Check cache: return same URL for retries of the same timestamp
    std::string streamUrl;
    {
      std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSeekTime).count();

      if (timestamp == m_lastSeekTimestamp && elapsed < 10000 && !m_lastStreamUrl.empty())
      {
        streamUrl = m_lastStreamUrl;
        kodi::Log(ADDON_LOG_DEBUG,
                  "StreamRedirectProxy: cache hit for t=%lld (elapsed=%lldms)",
                  static_cast<long long>(timestamp), static_cast<long long>(elapsed));
      }
    }

    if (streamUrl.empty())
    {
      // Generate a fresh session ID for each new seek to avoid CDN
      // rejecting the request due to the previous session still being
      // considered "active" on the server side.
      streamUrl = BuildEncryptedUrl(timestamp);

      {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        m_lastSeekTimestamp = timestamp;
        m_lastStreamUrl = streamUrl;
        m_lastSeekTime = std::chrono::steady_clock::now();
      }

      kodi::Log(ADDON_LOG_DEBUG,
                "StreamRedirectProxy: seek to t=%lld -> new encrypted URL (new session)",
                static_cast<long long>(timestamp));
    }

    // Use 302 redirect to the encrypted URL. FFmpeg follows the redirect
    // and uses the redirect target as the base URL for resolving relative
    // sub-playlist/segment URLs. This avoids 3-level playlist nesting issues
    // that occurred with the master playlist approach.
    std::string response = "HTTP/1.1 302 Found\r\n"
                           "Location: " + streamUrl + "\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n\r\n";
    send(clientSocket, response.c_str(), response.size(), 0);
    close(clientSocket);
  }
}

std::string StreamRedirectProxy::BuildEncryptedUrl(time_t timestamp)
{
  StreamParams params;
  {
    std::lock_guard<std::mutex> lock(m_paramsMutex);
    params = m_params;
  }

  // Use server time offset to produce accurate ctime (matches server clock)
  int64_t localMs = static_cast<int64_t>(time(nullptr)) * 1000;
  int64_t serverMs = localMs + params.serverTimeOffsetMs;
  std::string ctime = std::to_string(serverMs);

  // Generate a fresh session ID for each seek so the CDN doesn't see
  // a conflict with the previous stream that may still be "active".
  std::string sessionId = proxy_generate_uuid();

  kodi::Log(ADDON_LOG_DEBUG,
            "StreamRedirectProxy: ctime=%s (offset=%lldms) t=%lld session=%s",
            ctime.c_str(), static_cast<long long>(params.serverTimeOffsetMs),
            static_cast<long long>(timestamp), sessionId.c_str());

  std::string plain_aes;

  if (params.platform == PROXY_PLATFORM_ANDROIDTV)
  {
    plain_aes = "channel=" + params.publishingPoint + ";"
                "stream=" + params.streamingProfile + ";"
                "sp=" + params.serviceProvider + ";"
                "u=" + params.streamUser + ";"
                "m=" + params.serverIp + ";"
                "device=" + params.deviceNumber + ";"
                "ctime=" + ctime + ";"
                "lang=eng;player=" + PROXY_PLAYER + ";"
                "aa=" + (params.aaEnabled ? "true" : "false") + ";"
                "conn=" + PROXY_CONN_TYPE_ETHERNET + ";"
                "minvbr=100;"
                "ss=" + params.streamKey + ";"
                "session=" + sessionId + ";"
                "maxvbr=" + std::to_string(params.maxBitrate) +
                ";t=" + std::to_string(static_cast<long long>(timestamp)) + "000;";
  }
  else
  {
    plain_aes = "channel=" + params.publishingPoint + ";"
                "stream=" + params.streamingProfile + ";"
                "sp=" + params.serviceProvider + ";"
                "u=" + params.streamUser + ";"
                "ss=" + params.streamKey + ";"
                "minvbr=100;adaptive=true;player=" + PROXY_PLAYER + ";"
                "sig=" + params.sig + ";"
                "session=" + sessionId + ";"
                "m=" + params.serverIp + ";"
                "device=" + params.deviceNumber + ";"
                "ctime=" + ctime + ";"
                "conn=" + PROXY_CONN_TYPE_BROWSER + ";"
                "t=" + std::to_string(static_cast<long long>(timestamp)) + "000;"
                "aa=" + (params.aaEnabled ? "true" : "false");
  }

  kodi::Log(ADDON_LOG_DEBUG, "StreamRedirectProxy: plain_aes=%s", plain_aes.c_str());

  std::string key = base64_decode(proxy_urlsafedecode(params.streamKey));

  std::ostringstream ivConvert;
  for (int i = 0; i < PROXY_BLOCK_SIZE; i++)
    ivConvert << static_cast<uint8_t>(rand());
  std::string iv_str = ivConvert.str();

  std::string enc_str = proxy_aes_encrypt_cbc(iv_str, key, plain_aes);

  std::string enc_url = "https://" + params.serverHostname +
                         "/stream?i=" + proxy_urlsafeencode(base64_encode(iv_str.c_str(), iv_str.length())) +
                         "&a=" + proxy_urlsafeencode(base64_encode(enc_str.c_str(), enc_str.length()));

  if (params.platform == PROXY_PLATFORM_ANDROIDTV)
    enc_url += "&lang=eng";

  enc_url += "&sp=" + params.serviceProvider +
             "&u=" + params.streamUser +
             "&player=" + PROXY_PLAYER +
             "&session=" + sessionId;

  if (params.platform != PROXY_PLATFORM_ANDROIDTV)
    enc_url += "&sig=" + params.sig;

  kodi::Log(ADDON_LOG_DEBUG, "StreamRedirectProxy: enc_url=%s", enc_url.c_str());

  return enc_url;
}
