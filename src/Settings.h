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

  const std::string& GetHrtiUsername() const { return m_hrtiUsername; }
  const std::string& GetHrtiPassword() const { return m_hrtiPassword; }
  const std::string& GetHrtiIpAddress() const { return m_hrtiIpAddress; }
  const std::string& GetHrtiToken() const { return m_hrtiToken; }
  const std::string& GetHrtiDeviceID() const { return m_hrtiDeviceID; }

private:
  std::string m_hrtiUsername;
  std::string m_hrtiPassword;
  std::string m_hrtiIpAddress;
  std::string m_hrtiToken;
  std::string m_hrtiDeviceID;
};
