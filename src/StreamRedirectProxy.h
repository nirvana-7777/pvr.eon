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
#include <map>
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
  // Video bitrate of the selected profile. minvbr/maxvbr are video bounds, so
  // an audio-only profile must send 0 here -- see getVideoBitrate() comment in
  // PVREon.cpp.
  unsigned int videoBitrate = 0;
  // Fallback source for the ctime every seek URL embeds (see
  // BuildEncryptedUrl), used only when the offset below is unknown or stale.
  std::string apiTimeUrl;
  // Difference between the backend clock and this device's, as last measured
  // by CPVREon::GetTime(). The CDN rejects an encrypted URL whose ctime is
  // more than ~20 seconds old, which this satisfies: the offset is applied to
  // the current device clock, so the result is a present-tense timestamp, and
  // it is re-measured often enough that drift stays far inside that window.
  int64_t serverTimeOffsetMs = 0;
  int64_t serverTimeMeasuredAtMs = 0;
  bool serverTimeKnown = false;
  std::string accessToken;
  // Sent as the User-Agent for the proxy's own HTTP calls (time fetch,
  // verify-fetch) -- the CDN blocks requests missing a recognized
  // smart-device UA the same way it does for the main manifest fetch
  // (see issue #16 / EonParameters[m_platform].user_agent).
  std::string userAgent;
  // 0 = default (let ffmpeg pick), 1 = highest bitrate, 2 = lowest bitrate.
  // Applied to every URL this proxy mints, not just the initial one, so a
  // pinned quality survives seeks instead of resetting on each one.
  int qualityPreference = 0;
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

  // Registers one playback session's parameters and returns the id to embed
  // in the URLs handed to inputstream.ffmpegdirect (see
  // catchup_url_format_string in PVREon.cpp).
  //
  // Sessions are kept side by side instead of one set of params being
  // overwritten because Kodi can have two stream opens in flight at once:
  // it calls GetEPGTagStreamProperties twice for a single EPG playback (once
  // to read EPGPlaybackAsLive, once via StartPlayback), and its
  // autoplay-next-programme can fire at the same moment the user picks
  // something else in the guide. A URL already handed out has to keep
  // resolving after that -- tearing the proxy down and relistening on a
  // fresh port made the earlier open fail outright with "playback failed".
  int RegisterSession(const StreamParams& params);

private:
  void ServerThread();
  bool GetSessionParams(int sessionId, StreamParams& params);
  static std::string BuildEncryptedUrl(const StreamParams& params, time_t timestamp);

  int m_port = 0;
  // Sockets are stored as intptr_t so one header serves both socket APIs:
  // on POSIX it holds the int descriptor, on Windows the SOCKET handle --
  // whose INVALID_SOCKET value is ~0 and therefore -1 once cast, so the
  // "invalid is negative" convention below works unchanged on both.
  intptr_t m_serverSocket = -1;
  // Wake channel used to interrupt ServerThread's select() from Stop() --
  // a self-pipe on POSIX, a loopback UDP socket pair on Windows (winsock's
  // select() only watches sockets). Closing m_serverSocket from the stopping
  // thread instead raced with ServerThread's own use of it: it could
  // FD_SET(-1) (undefined behaviour -- FD_SET indexes the fd_set bitmap, so
  // a negative fd writes out of bounds) or accept() on a descriptor number
  // the OS had already reused. ServerThread alone owns m_serverSocket and
  // closes it when the loop exits.
  intptr_t m_wakePipe[2] = {-1, -1};
  std::atomic<bool> m_running{false};
  std::thread m_thread;

  std::mutex m_sessionsMutex;
  std::map<int, StreamParams> m_sessions;
  int m_nextSessionId = 1;
  int m_latestSessionId = 0;

  // Reuse the same encrypted URL for rapid repeated requests at the same
  // session and timestamp (ffmpegdirect retries), instead of minting a new
  // CDN session each time.
  std::mutex m_cacheMutex;
  int m_lastSeekSessionId = 0;
  time_t m_lastSeekTimestamp = 0;
  std::string m_lastStreamUrl;
  std::chrono::steady_clock::time_point m_lastSeekTime;
};
