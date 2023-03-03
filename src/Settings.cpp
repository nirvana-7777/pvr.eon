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
  if (!kodi::addon::CheckSettingString("username", m_hrtiUsername))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'username' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("password", m_hrtiPassword))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'password' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("ipaddress", m_hrtiIpAddress))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'ipaddress' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("token", m_hrtiToken))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'token' setting");
    return false;
  }

  if (!kodi::addon::CheckSettingString("deviceid", m_hrtiDeviceID))
  {
    /* If setting is unknown fallback to defaults */
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'deviceid' setting");
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
    tmp_sUsername = m_hrtiUsername;
    m_hrtiUsername = settingValue;
    if (tmp_sUsername != m_hrtiUsername)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (settingName == "password")
  {
    std::string tmp_sPassword;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'password'");
    tmp_sPassword = m_hrtiPassword;
    m_hrtiPassword = settingValue;
    if (tmp_sPassword != m_hrtiPassword)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (settingName == "ipaddress")
  {
    std::string tmp_sIpAddress;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'ipaddress'");
    tmp_sIpAddress = m_hrtiIpAddress;
    m_hrtiIpAddress = settingValue;
    if (tmp_sIpAddress != m_hrtiIpAddress)
    {
      kodi::addon::SetSettingString("ipaddress", m_hrtiIpAddress);
//      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "token")
  {
    std::string tmp_sToken;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'token'");
    tmp_sToken = m_hrtiToken;
    m_hrtiToken = settingValue;
    if (tmp_sToken != m_hrtiToken)
    {
      kodi::addon::SetSettingString("token", m_hrtiToken);
  //      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "deviceid")
  {
    std::string tmp_sDeviceID;
    kodi::Log(ADDON_LOG_DEBUG, "Changed Setting 'deviceid'");
    tmp_sDeviceID = m_hrtiDeviceID;
    m_hrtiDeviceID = settingValue;
    if (tmp_sDeviceID != m_hrtiDeviceID)
    {
      kodi::addon::SetSettingString("deviceid", m_hrtiDeviceID);
    //      return ADDON_STATUS_NEED_RESTART;
    }
  }

  return ADDON_STATUS_OK;
}

bool CSettings::VerifySettings() {
  std::string username = GetHrtiUsername();
  std::string password = GetHrtiPassword();
  if (username.empty() || password.empty()) {
    kodi::Log(ADDON_LOG_INFO, "Username or password not set.");
    kodi::QueueNotification(QUEUE_WARNING, "", kodi::addon::GetLocalizedString(30200));

    return false;
  }
  return true;
}
