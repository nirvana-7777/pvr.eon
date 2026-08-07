/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

// Parameters needed to build a fresh, correctly time-stamped and signed
// EON stream URL for a given seek position. Captured once when playback
// starts so the proxy can regenerate URLs without further API calls.
struct StreamParams
{
  std::string publishingPoint;
  std::string streamingProfile;
  std::string serviceProvider;
  std::string streamUser;
  std::string streamKey;
  std::string serverIp;
  std::string serverHostname;
  std::string deviceNumber;
  std::string sig;
  bool aaEnabled = false;
  int platform = 0;
  unsigned int maxBitrate = 0;
  // Used to fetch a fresh ctime for every seek (see BuildEncryptedUrl) --
  // the CDN rejects an encrypted URL if its embedded ctime is more than
  // ~20 seconds old, so a cached device-clock-relative offset isn't
  // reliable enough; matches how the non-proxy path calls this API fresh
  // for every request.
  std::string apiTimeUrl;
  std::string accessToken;
  // Sent as the User-Agent for the proxy's own HTTP calls (time fetch,
  // verify-fetch) -- the CDN blocks requests missing a recognized
  // smart-device UA the same way it does for the main manifest fetch
  // (see issue #16 / EonParameters[m_platform].user_agent).
  std::string userAgent;
};

// A local loopback HTTP server used as inputstream.ffmpegdirect's
// catchup_url_format_string target. EON encodes the seek timestamp inside
// an AES-CBC encrypted payload, which ffmpegdirect's {utc} substitution
// cannot produce on its own. This proxy receives ffmpegdirect's seek
// requests, builds a fresh encrypted URL (with a new session id, to avoid
// the CDN rejecting a session it still considers active) and 302-redirects
// to it.
class StreamRedirectProxy
{
public:
  StreamRedirectProxy();
  ~StreamRedirectProxy();

  bool Start();
  void Stop();
  int GetPort() const { return m_port; }

  void SetStreamParams(const StreamParams& params);

private:
  void ServerThread();
  std::string BuildEncryptedUrl(time_t timestamp);

  int m_port = 0;
  int m_serverSocket = -1;
  std::atomic<bool> m_running{false};
  std::thread m_thread;

  std::mutex m_paramsMutex;
  StreamParams m_params;

  // Reuse the same encrypted URL for rapid repeated requests at the same
  // timestamp (ffmpegdirect retries), instead of minting a new session
  // each time.
  std::mutex m_cacheMutex;
  time_t m_lastSeekTimestamp = 0;
  std::string m_lastStreamUrl;
  std::chrono::steady_clock::time_point m_lastSeekTime;
};
