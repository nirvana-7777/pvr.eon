/*
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <kodi/AddonBase.h>

class ATTR_DLL_LOCAL CSettings
{
public:
  CSettings() = default;

  bool Load();
  ADDON_STATUS SetSetting(const std::string& settingName, const std::string& settingValue);
  bool VerifySettings();

  const int& GetEonServiceProvider() const { return m_eonServiceProvider; }
  const int& GetPlatform() const { return m_eonPlatform; }
  const int& GetInputstream() const { return m_eonInputstream; }
  // 0 = default (let ffmpegdirect/ffmpeg pick), 1 = highest bitrate, 2 = lowest bitrate.
  const int& GetFfmpegdirectQuality() const { return m_eonFfmpegdirectQuality; }
  const int& GetAgeRating() const { return m_eonAgeRating; }
  const std::string& GetEonUsername() const { return m_eonUsername; }
  const std::string& GetEonPassword() const { return m_eonPassword; }
  const std::string& GetEonAccessToken() const { return m_eonAccessToken; }
  const std::string& GetEonRefreshToken() const { return m_eonRefreshToken; }
  const std::string& GetEonDeviceNumber() const { return m_eonDeviceNumber; }
  const std::string& GetEonDeviceID() const { return m_eonDeviceID; }
  const std::string& GetEonDeviceSerial() const { return m_eonDeviceSerial; }
  const std::string& GetEonSubscriberID() const { return m_eonSubscriberID; }
  const std::string& GetEonStreamKey() const { return m_eonStreamKey; }
  const std::string& GetEonStreamUser() const { return m_eonStreamUser; }
  const std::string& GetGenericAccessToken() const { return m_Generic_AccessToken; }
  const std::string& GetSSAccessToken() const { return m_SS_AccessToken; }
  const std::string& GetSSRefreshToken() const { return m_SS_RefreshToken; }
  const std::string& GetSSIdentity() const { return m_SS_Identity; }
  const int& GetStartNum() const { return m_start_num; }
  const bool HideUnsubscribed() const { return m_hideunsubscribed; }
  const bool IsTVenabled() const { return m_enabletv; }
  const bool IsRadioenabled() const { return m_enableradio; }
  const bool IsGroupsenabled() const  { return m_enablegroups; }
  const bool UseShortNames() const  { return m_shortnames; }
  const bool UseExperimentalNativeStream() const { return m_experimentalNativeStream; }
  const bool UseCustomUserAgent() const { return m_useCustomUserAgent; }
  const std::string& GetCustomUserAgent() const { return m_customUserAgent; }

private:
  int m_eonServiceProvider = 0;
  int m_eonPlatform = 0;
  int m_eonInputstream = 0;
  int m_eonFfmpegdirectQuality = 0;
  int m_start_num = 1;
  int m_eonAgeRating = 18;
  std::string m_eonUsername;
  std::string m_eonPassword;
  std::string m_eonAccessToken;
  std::string m_eonRefreshToken;
  std::string m_eonDeviceNumber;
  std::string m_eonDeviceID;
  std::string m_eonDeviceSerial;
  std::string m_eonSubscriberID;
  std::string m_eonStreamKey;
  std::string m_eonStreamUser;
  std::string m_Generic_AccessToken;
  std::string m_SS_AccessToken;
  std::string m_SS_RefreshToken;
  std::string m_SS_Identity;
  std::string m_customUserAgent;
  bool m_hideunsubscribed = false;
  bool m_enabletv = true;
  bool m_enableradio = true;
  bool m_enablegroups = true;
  bool m_shortnames = false;
  bool m_experimentalNativeStream = false;
  bool m_useCustomUserAgent = false;
};
