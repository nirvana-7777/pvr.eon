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

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>

#ifdef _WIN32
// Winsock replaces the POSIX socket headers; every further difference
// (handle type, close call, timeouts, the wake mechanism) is confined to
// the portability helpers at the top of the anonymous namespace below.
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
// ---------------------------------------------------------------------------
// Socket portability. Sockets live in intptr_t members (see the header);
// these helpers hide the only API differences the proxy touches.
// ---------------------------------------------------------------------------
#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr int kProxyShutRdwr = SD_BOTH;

inline NativeSocket Native(intptr_t s) { return static_cast<NativeSocket>(s); }
inline void CloseSocket(intptr_t s) { closesocket(Native(s)); }

// Winsock wants one-time per-process initialisation before any socket call.
// Lazily done on the first Start(); WSACleanup is deliberately never called,
// since the addon lives for the whole Kodi process anyway.
inline bool EnsureSocketsInitialised()
{
  static const bool initialised = [] {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
  }();
  return initialised;
}

// select() on Windows only watches sockets, so the POSIX self-pipe is
// replaced by a pair of loopback UDP sockets: wake[0] is a bound receiver
// the select() watches, wake[1] a connected sender Stop() pokes it with.
inline bool CreateWakePair(intptr_t wake[2])
{
  NativeSocket recvSock = socket(AF_INET, SOCK_DGRAM, 0);
  if (recvSock == INVALID_SOCKET)
    return false;

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;

  int addrLen = sizeof(addr);
  if (bind(recvSock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0 ||
      getsockname(recvSock, reinterpret_cast<struct sockaddr*>(&addr), &addrLen) != 0)
  {
    closesocket(recvSock);
    return false;
  }

  NativeSocket sendSock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sendSock == INVALID_SOCKET)
  {
    closesocket(recvSock);
    return false;
  }
  if (connect(sendSock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0)
  {
    closesocket(recvSock);
    closesocket(sendSock);
    return false;
  }

  wake[0] = static_cast<intptr_t>(recvSock);
  wake[1] = static_cast<intptr_t>(sendSock);
  return true;
}

inline void SignalWake(intptr_t writeEnd)
{
  const char wakeByte = 'x';
  send(Native(writeEnd), &wakeByte, 1, 0);
}
#else
using NativeSocket = int;
constexpr int kProxyShutRdwr = SHUT_RDWR;

inline NativeSocket Native(intptr_t s) { return static_cast<NativeSocket>(s); }
inline void CloseSocket(intptr_t s) { close(Native(s)); }
inline bool EnsureSocketsInitialised() { return true; }

inline bool CreateWakePair(intptr_t wake[2])
{
  int fds[2];
  if (pipe(fds) < 0)
    return false;
  wake[0] = fds[0];
  wake[1] = fds[1];
  return true;
}

inline void SignalWake(intptr_t writeEnd)
{
  const char wakeByte = 'x';
  const ssize_t written = write(Native(writeEnd), &wakeByte, 1);
  (void)written;
}
#endif

constexpr uint8_t kAesBlockSize = 16;
// Matches PLATFORM_ANDROIDTV in PVREon.h. Kept as a local literal to avoid
// pulling in the full PVR client header from this standalone proxy.
constexpr int kAndroidTvPlatform = 1;
// How long a redirect for the same seek timestamp is reused instead of
// minting a fresh encrypted URL, to absorb ffmpegdirect's retry requests.
constexpr auto kSeekCacheWindow = std::chrono::milliseconds(10000);
// Only the newest few sessions can realistically still have URLs in flight
// (see RegisterSession); retaining more would just leak stream credentials
// for playbacks that ended long ago.
constexpr size_t kMaxRetainedSessions = 4;

// Reads an integer query parameter out of a raw request line, e.g. "s" from
// "GET /stream?s=3&t=1787560500 HTTP/1.1". Anchored on the "?"/"&" that must
// precede a parameter name so a bare find("t=") cannot match inside the
// path or another value.
bool GetQueryParam(const std::string& request, const std::string& name, long long& value)
{
  for (const std::string& prefix : {"?" + name + "=", "&" + name + "="})
  {
    const size_t pos = request.find(prefix);
    if (pos == std::string::npos)
      continue;

    const size_t start = pos + prefix.size();
    const size_t end = request.find_first_of("& \r\n", start);
    try
    {
      value = std::stoll(request.substr(start, end - start));
      return true;
    }
    catch (...)
    {
      return false;
    }
  }
  return false;
}

// Mirrors PLAYER/CONN_TYPE_* in Globals.h. Not reused directly because
// Globals.h's EON_USER_AGENT needs KODI_VERSION, which is only defined
// transitively via kodi/addon-instance/PVR.h (pulled in by PVREon.h, not
// by this standalone proxy).
const std::string kPlayer = "m3u8";
const std::string kConnTypeEthernet = "ETHERNET";
const std::string kConnTypeBrowser = "BROWSER";

// How old a measured backend/device clock offset may be before this falls back
// to asking the backend again. Well inside the ~20s freshness the CDN wants,
// even for a clock drifting far worse than a real one does.
constexpr int64_t SERVER_TIME_OFFSET_MAX_AGE_MS = 60 * 60 * 1000;

// The ctime a seek URL embeds has to be current -- the CDN rejects one more
// than ~20 seconds old -- but current is not the same as freshly fetched: the
// device clock plus the backend's measured offset is just as present-tense,
// and costs no round trip. Only a stream whose offset was never measured, or
// was measured long enough ago that drift is worth worrying about, pays for
// the request. Mirrors CPVREon::GetTime().
std::string FetchServerTime(const StreamParams& params)
{
  const int64_t deviceMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
  if (params.serverTimeKnown &&
      deviceMs - params.serverTimeMeasuredAtMs < SERVER_TIME_OFFSET_MAX_AGE_MS)
  {
    return std::to_string(deviceMs + params.serverTimeOffsetMs);
  }

  const int64_t fallbackMs = deviceMs;
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

  if (!EnsureSocketsInitialised())
  {
    kodi::Log(ADDON_LOG_ERROR, "StreamRedirectProxy: socket layer initialisation failed");
    return false;
  }

  m_serverSocket = static_cast<intptr_t>(socket(AF_INET, SOCK_STREAM, 0));
  if (m_serverSocket < 0)
  {
    kodi::Log(ADDON_LOG_ERROR, "StreamRedirectProxy: failed to create socket");
    return false;
  }

  int opt = 1;
  // The const char* cast serves both APIs: winsock wants char*, POSIX void*.
  setsockopt(Native(m_serverSocket), SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char*>(&opt), sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0; // Let the OS pick a port

  if (bind(Native(m_serverSocket), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
  {
    kodi::Log(ADDON_LOG_ERROR, "StreamRedirectProxy: failed to bind");
    CloseSocket(m_serverSocket);
    m_serverSocket = -1;
    return false;
  }

  socklen_t addrLen = sizeof(addr);
  getsockname(Native(m_serverSocket), reinterpret_cast<struct sockaddr*>(&addr), &addrLen);
  m_port = ntohs(addr.sin_port);

  if (listen(Native(m_serverSocket), 5) < 0)
  {
    kodi::Log(ADDON_LOG_ERROR, "StreamRedirectProxy: failed to listen");
    CloseSocket(m_serverSocket);
    m_serverSocket = -1;
    return false;
  }

  if (!CreateWakePair(m_wakePipe))
  {
    kodi::Log(ADDON_LOG_ERROR, "StreamRedirectProxy: failed to create wake channel");
    CloseSocket(m_serverSocket);
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

  // Wake ServerThread's select() without touching m_serverSocket -- see the
  // m_wakePipe comment in the header.
  if (m_wakePipe[1] >= 0)
    SignalWake(m_wakePipe[1]);

  if (m_thread.joinable())
    m_thread.join();

  for (intptr_t& fd : m_wakePipe)
  {
    if (fd >= 0)
    {
      CloseSocket(fd);
      fd = -1;
    }
  }

  kodi::Log(ADDON_LOG_INFO, "StreamRedirectProxy: stopped");
}

int StreamRedirectProxy::RegisterSession(const StreamParams& params)
{
  std::lock_guard<std::mutex> lock(m_sessionsMutex);

  const int sessionId = m_nextSessionId++;
  m_sessions[sessionId] = params;
  m_latestSessionId = sessionId;

  while (m_sessions.size() > kMaxRetainedSessions)
    m_sessions.erase(m_sessions.begin());

  return sessionId;
}

bool StreamRedirectProxy::GetSessionParams(int sessionId, StreamParams& params)
{
  std::lock_guard<std::mutex> lock(m_sessionsMutex);

  auto it = m_sessions.find(sessionId);
  if (it == m_sessions.end())
  {
    // Unknown or already-evicted session: serve it with the newest params
    // rather than failing the request outright, which keeps a stale URL
    // playing the right channel in the common case where only one stream
    // is actually open.
    it = m_sessions.find(m_latestSessionId);
    if (it == m_sessions.end())
      return false;

    kodi::Log(ADDON_LOG_DEBUG,
              "StreamRedirectProxy: unknown session %d, falling back to newest (%d)", sessionId,
              m_latestSessionId);
  }

  params = it->second;
  return true;
}

void StreamRedirectProxy::ServerThread()
{
  while (m_running)
  {
    const intptr_t listenFd = m_serverSocket;
    const intptr_t wakeFd = m_wakePipe[0];
    if (listenFd < 0)
      break;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(Native(listenFd), &readfds);
    if (wakeFd >= 0)
      FD_SET(Native(wakeFd), &readfds);

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    // Winsock ignores select()'s first argument; POSIX wants max fd + 1.
    int ret = select(static_cast<int>(std::max(listenFd, wakeFd)) + 1, &readfds, nullptr,
                     nullptr, &tv);
    if (ret <= 0)
      continue;

    // Stop() asked us to exit.
    if (wakeFd >= 0 && FD_ISSET(Native(wakeFd), &readfds))
      break;

    if (!FD_ISSET(Native(listenFd), &readfds))
      continue;

    const intptr_t clientSocket = static_cast<intptr_t>(accept(Native(listenFd), nullptr, nullptr));
    if (clientSocket < 0)
      continue;

    // A client that connects but never finishes its request (or never reads
    // the reply) must not stall the only thread accepting connections.
#ifdef _WIN32
    // Winsock expresses socket timeouts as DWORD milliseconds, not timeval.
    const DWORD clientTimeout = 5000;
#else
    struct timeval clientTimeout;
    clientTimeout.tv_sec = 5;
    clientTimeout.tv_usec = 0;
#endif
    setsockopt(Native(clientSocket), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&clientTimeout), sizeof(clientTimeout));
    setsockopt(Native(clientSocket), SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char*>(&clientTimeout), sizeof(clientTimeout));

    char buf[2048];
    int bytesRead = static_cast<int>(recv(Native(clientSocket), buf, sizeof(buf) - 1, 0));
    if (bytesRead <= 0)
    {
      CloseSocket(clientSocket);
      continue;
    }
    buf[bytesRead] = '\0';

    // Parse "GET /stream?s=<session>&t=<timestamp> HTTP/...". t=0 is a
    // sentinel meaning "live" (no historical offset at all) -- see
    // BuildEncryptedUrlForSession, used for ffmpegdirect's default_url,
    // which it falls back to whenever a seek resolves close enough to live.
    const std::string request(buf);

    long long rawTimestamp = 0;
    if (!GetQueryParam(request, "t", rawTimestamp) || rawTimestamp < 0)
    {
      std::string response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
      send(Native(clientSocket), response.c_str(), static_cast<int>(response.size()), 0);
      CloseSocket(clientSocket);
      continue;
    }
    const time_t timestamp = static_cast<time_t>(rawTimestamp);

    long long rawSession = 0;
    const int sessionId =
        GetQueryParam(request, "s", rawSession) ? static_cast<int>(rawSession) : 0;

    StreamParams params;
    if (!GetSessionParams(sessionId, params))
    {
      kodi::Log(ADDON_LOG_ERROR, "StreamRedirectProxy: no stream params registered, cannot serve request");
      std::string response = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n";
      send(Native(clientSocket), response.c_str(), static_cast<int>(response.size()), 0);
      CloseSocket(clientSocket);
      continue;
    }

    // Reuse the same encrypted URL for retries of the same session and
    // timestamp. Keyed on the session too: two concurrent streams can ask
    // for the same timestamp and must not be handed each other's URL.
    std::string streamUrl;
    {
      std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
      const auto elapsed = std::chrono::steady_clock::now() - m_lastSeekTime;
      if (sessionId == m_lastSeekSessionId && timestamp == m_lastSeekTimestamp &&
          elapsed < kSeekCacheWindow && !m_lastStreamUrl.empty())
      {
        streamUrl = m_lastStreamUrl;
        kodi::Log(ADDON_LOG_DEBUG, "StreamRedirectProxy: cache hit for s=%d t=%lld", sessionId,
                  static_cast<long long>(timestamp));
      }
    }

    if (streamUrl.empty())
    {
      // A fresh CDN session id avoids the request being rejected because
      // the previous one is still considered "active" server-side.
      streamUrl = BuildEncryptedUrl(params, timestamp);

      {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        m_lastSeekSessionId = sessionId;
        m_lastSeekTimestamp = timestamp;
        m_lastStreamUrl = streamUrl;
        m_lastSeekTime = std::chrono::steady_clock::now();
      }

      kodi::Log(ADDON_LOG_DEBUG, "StreamRedirectProxy: seek to s=%d t=%lld -> new encrypted URL",
                sessionId, static_cast<long long>(timestamp));
    }

    // 302 redirect (rather than proxying the body) so FFmpeg resolves
    // relative HLS segment/variant URLs against the real CDN host.
    std::string response = "HTTP/1.1 302 Found\r\n"
                           "Location: " + streamUrl + "\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n\r\n";
    send(Native(clientSocket), response.c_str(), static_cast<int>(response.size()), 0);
    CloseSocket(clientSocket);
  }

  // Owned solely by this thread -- see the m_wakePipe comment in the header.
  if (m_serverSocket >= 0)
  {
    shutdown(Native(m_serverSocket), kProxyShutRdwr);
    CloseSocket(m_serverSocket);
    m_serverSocket = -1;
  }
}

namespace
{
std::string BuildEncryptedUrlForSession(const StreamParams& params, time_t timestamp,
                                         const std::string& ctime, const std::string& sessionId)
{
  // t=0 is a sentinel for "live" -- requesting a historical/catchup URL for
  // a timestamp that's essentially "right now" isn't equivalent to a real
  // live request: the CDN's replay path returned an immediate EOF instead
  // of content when tried (presumably an encoding/ingestion lag before
  // "now" becomes available there). A genuine live request omits the "t="
  // field entirely -- see BuildPlaybackUrl's isLive branch in PVREon.cpp,
  // which this mirrors.
  const bool isLiveRequest = timestamp <= 0;

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
                "minvbr=" + std::to_string(std::min(100u, params.videoBitrate)) + ";"
                "ss=" + params.streamKey + ";"
                "session=" + sessionId + ";"
                "maxvbr=" + std::to_string(params.videoBitrate) + ";";
    if (!isLiveRequest)
      plain_aes += "t=" + std::to_string(static_cast<long long>(timestamp)) + "000;";
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
                "minvbr=" + std::to_string(std::min(100u, params.videoBitrate)) + ";"
                "sig=" + params.sig + ";"
                "session=" + sessionId + ";"
                "m=" + params.serverIp + ";"
                "device=" + params.deviceNumber + ";"
                "ctime=" + ctime + ";"
                "conn=" + kConnTypeBrowser + ";"
                "player=" + kPlayer + ";";
    if (!isLiveRequest)
      plain_aes += "t=" + std::to_string(static_cast<long long>(timestamp)) + "000;";
    plain_aes += std::string("aa=") + (params.aaEnabled ? "true" : "false");
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

std::string StreamRedirectProxy::BuildEncryptedUrl(const StreamParams& params, time_t timestamp)
{
  const std::string ctime = FetchServerTime(params);
  const std::string sessionId = Utils::CreateUUID();
  std::string enc_url = BuildEncryptedUrlForSession(params, timestamp, ctime, sessionId);

  if (params.qualityPreference != 0)
  {
    // Same rewrite as the initial (non-proxy) stream open: fetch the master
    // playlist this URL points to and rewrite to the chosen variant's own
    // playlist URL, so a pinned quality survives every seek too.
    Curl qualityCurl;
    if (!params.userAgent.empty())
      qualityCurl.AddHeader("User-Agent", params.userAgent);
    int qualityStatus = 0;
    const std::string masterPlaylist = qualityCurl.Get(enc_url, qualityStatus);
    const std::string variantUrl =
        Utils::SelectVariantPlaylistUrl(masterPlaylist, enc_url, params.qualityPreference);
    if (!variantUrl.empty())
      enc_url = variantUrl;
    else
      kodi::Log(ADDON_LOG_ERROR,
                "StreamRedirectProxy: quality override requested but no variant found, using default URL");
  }

  kodi::Log(ADDON_LOG_DEBUG, "StreamRedirectProxy: t=%lld -> %s",
            static_cast<long long>(timestamp), enc_url.c_str());

  return enc_url;
}
