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

private:
  int m_eonServiceProvider;
  int m_eonPlatform;
  int m_eonInputstream;
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
  int m_start_num;
  bool m_hideunsubscribed;
  bool m_enabletv;
  bool m_enableradio;
  bool m_enablegroups;
  bool m_shortnames;
};
