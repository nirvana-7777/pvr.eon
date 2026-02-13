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
  std::string sessionId;
  bool aaEnabled = false;
  int platform = 0;
  unsigned int maxBitrate = 0;
  // Server time offset: serverTimeMs - localTimeMs at the time params were created.
  // Used to generate accurate ctime values without calling the server API.
  int64_t serverTimeOffsetMs = 0;
};

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

  // Throttle: cache the last generated URL to reuse for rapid repeated seeks
  std::mutex m_cacheMutex;
  time_t m_lastSeekTimestamp = 0;
  std::string m_lastStreamUrl;
  std::chrono::steady_clock::time_point m_lastSeekTime;
};
