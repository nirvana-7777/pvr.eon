/*
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Settings.h"
#include <kodi/General.h>

bool CSettings::Load()
{
  if (!kodi::addon::CheckSettingString("username", m_eonUsername))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'username' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("password", m_eonPassword))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'password' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("accesstoken", m_eonAccessToken))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'access token' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("refreshtoken", m_eonRefreshToken))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'refresh token' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("deviceid", m_eonDeviceID))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'deviceid' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("devicenumber", m_eonDeviceNumber))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'devicenumber' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("deviceserial", m_eonDeviceSerial))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'deviceserial' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("subscriberid", m_eonSubscriberID))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'subscriberid' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("streamkey", m_eonStreamKey))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'subscriberid' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("streamuser", m_eonStreamUser))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'subscriberid' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingBoolean("hideunsubscribed", m_hideunsubscribed))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'hideunsubscribed' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingBoolean("enabletv", m_enabletv))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'enabletv' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingBoolean("enableradio", m_enableradio))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'enableradio' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingBoolean("enablegroups", m_enablegroups))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'enablegroups' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingBoolean("shortnames", m_shortnames))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'shortnames' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("genericaccesstoken", m_Generic_AccessToken))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'genericaccesstoken' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("ssaccesstoken", m_SS_AccessToken))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'ssaccesstoken' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("ssrefreshtoken", m_SS_RefreshToken))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'ssrefreshtoken' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("ssidentity", m_SS_Identity))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'ssidentity' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingInt("startnum", m_start_num))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'startnum' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingInt("provider", m_eonServiceProvider))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'provider' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingInt("platform", m_eonPlatform))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'platform' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingInt("inputstream", m_eonInputstream))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'inputstream' setting");
    return false;
  }

  return true;
}

ADDON_STATUS CSettings::SetSetting(const std::string& settingName,
                                   const std::string& settingValue)
{
  if (settingName == "username")
  {
    std::string tmp_sUsername;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'username'");
    tmp_sUsername = m_eonUsername;
    m_eonUsername = settingValue;
    if (tmp_sUsername != m_eonUsername)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (settingName == "password")
  {
    std::string tmp_sPassword;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'password'");
    tmp_sPassword = m_eonPassword;
    m_eonPassword = settingValue;
    if (tmp_sPassword != m_eonPassword)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (settingName == "accesstoken")
  {
    std::string tmp_sToken;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'accesstoken'");
    tmp_sToken = m_eonAccessToken;
    m_eonAccessToken = settingValue;
    if (tmp_sToken != m_eonAccessToken)
    {
      kodi::addon::SetSettingString("accesstoken", m_eonAccessToken);
  //      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "refreshtoken")
  {
    std::string tmp_sToken;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'refreshtoken'");
    tmp_sToken = m_eonRefreshToken;
    m_eonRefreshToken = settingValue;
    if (tmp_sToken != m_eonRefreshToken)
    {
      kodi::addon::SetSettingString("refreshtoken", m_eonRefreshToken);
  //      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "devicenumber")
  {
    std::string tmp_sDeviceNumber;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'devicenumber'");
    tmp_sDeviceNumber = m_eonDeviceNumber;
    m_eonDeviceNumber = settingValue;
    if (tmp_sDeviceNumber != m_eonDeviceNumber)
    {
      kodi::addon::SetSettingString("devicenumber", m_eonDeviceNumber);
    //      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "deviceid")
  {
    std::string tmp_sDeviceID;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'deviceid'");
    tmp_sDeviceID = m_eonDeviceID;
    m_eonDeviceID = settingValue;
    if (tmp_sDeviceID != m_eonDeviceID)
    {
      kodi::addon::SetSettingString("deviceid", m_eonDeviceID);
    //      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "deviceserial")
  {
    std::string tmp_sDeviceSerial;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'deviceserial'");
    tmp_sDeviceSerial = m_eonDeviceSerial;
    m_eonDeviceSerial = settingValue;
    if (tmp_sDeviceSerial != m_eonDeviceSerial)
    {
      kodi::addon::SetSettingString("deviceserial", m_eonDeviceSerial);
    //      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "subscriberid")
  {
    std::string tmp_sSubscriberID;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'subscriberid'");
    tmp_sSubscriberID = m_eonSubscriberID;
    m_eonSubscriberID = settingValue;
    if (tmp_sSubscriberID != m_eonSubscriberID)
    {
      kodi::addon::SetSettingString("subscriberid", m_eonSubscriberID);
    //      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "streamkey")
  {
    std::string tmp_sStreamKey;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'streamkey'");
    tmp_sStreamKey = m_eonStreamKey;
    m_eonStreamKey = settingValue;
    if (tmp_sStreamKey != m_eonStreamKey)
    {
      kodi::addon::SetSettingString("streamkey", m_eonStreamKey);
    //      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "streamuser")
  {
    std::string tmp_sStreamUser;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'streamuser'");
    tmp_sStreamUser = m_eonStreamUser;
    m_eonStreamUser = settingValue;
    if (tmp_sStreamUser != m_eonStreamUser)
    {
      kodi::addon::SetSettingString("streamuser", m_eonStreamUser);
    //      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "genericaccesstoken")
  {
    std::string tmp_sGenericAccessToken;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'genericaccesstoken'");
    tmp_sGenericAccessToken = m_Generic_AccessToken;
    m_Generic_AccessToken = settingValue;
    if (tmp_sGenericAccessToken != m_Generic_AccessToken)
    {
      kodi::addon::SetSettingString("genericaccesstoken", m_Generic_AccessToken);
    //      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "ssaccesstoken")
  {
    std::string tmp_sSSAccessToken;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'ssaccesstoken'");
    tmp_sSSAccessToken = m_SS_AccessToken;
    m_SS_AccessToken = settingValue;
    if (tmp_sSSAccessToken != m_SS_AccessToken)
    {
      kodi::addon::SetSettingString("ssaccesstoken", m_SS_AccessToken);
    //      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "ssrefreshtoken")
  {
    std::string tmp_sSSRefreshToken;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'ssrefreshtoken'");
    tmp_sSSRefreshToken = m_SS_RefreshToken;
    m_SS_RefreshToken = settingValue;
    if (tmp_sSSRefreshToken != m_SS_RefreshToken)
    {
      kodi::addon::SetSettingString("ssrefreshtoken", m_SS_RefreshToken);
    //      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "ssidentity")
  {
    std::string tmp_sSSIdentity;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'ssidentity'");
    tmp_sSSIdentity = m_SS_Identity;
    m_SS_Identity = settingValue;
    if (tmp_sSSIdentity != m_SS_Identity)
    {
      kodi::addon::SetSettingString("ssidentity", m_SS_Identity);
    //      return ADDON_STATUS_NEED_RESTART;
    }
  }

  return ADDON_STATUS_OK;
}

bool CSettings::VerifySettings() {
  std::string username = GetEonUsername();
  std::string password = GetEonPassword();
  if (username.empty() || password.empty()) {
    kodi::Log(ADDON_LOG_INFO, "Username or password not set.");
    kodi::QueueNotification(QUEUE_WARNING, "", kodi::addon::GetLocalizedString(30200));

    return false;
  }
  return true;
}
