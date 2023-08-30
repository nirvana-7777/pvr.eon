/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include <string>
#include <vector>

#include <kodi/addon-instance/PVR.h>
#include "Settings.h"
#include "http/HttpClient.h"
#include "rapidjson/document.h"

struct EonChannelCategory
{
  int id;
  bool primary;
};

struct EonPublishingPoint
{
  std::string publishingPoint;
  std::string audioLanguage;
  std::string subtitleLanguage;
  std::vector<int> profileIds;
};

struct EonChannel
{
  bool bRadio;
  bool bArchive;
  int iUniqueId;
//  int referenceID; // EON_ID
  int iChannelNumber; //position
  std::string strChannelName;
  std::string strIconPath;
  std::vector<EonChannelCategory> categories;
  std::vector<EonPublishingPoint> publishingPoints;
  std::string sig;
  bool aaEnabled;
  bool subscribed;
//  std::string strStreamURL;
};

struct EonServer
{
  std::string id;
  std::string ip;
  std::string hostname;
};

struct EonEvent
{
  std::string time;
  std::string type;
  std::string rnd_profile;
  std::string session_id;
  std::string device_id;
  std::string subscriber_id;
  std::string offset_time;
  std::string channel_id;
  std::string epd_id;
};

struct EonRenderingProfile
{
  int id;
  std::string name;
  std::string coreStreamId;
  int height;
  int width;
  int audioBitrate;
  int videoBitrate;
};

struct EonCategoryChannel
{
  int id;
  int position;
};

struct EonCategory
{
  std::string name;
  int id;
  int order;
  std::vector<EonCategoryChannel> channels;
  bool isRadio;
  bool isDefault;
};

struct EonCDN
{
  int id;
  std::string identifier;
  std::string baseApi;
  bool isDefault;
};

struct EonParameters
{
  std::string api_prefix;
  std::string api_selector;
  std::string device_type;
  std::string device_mac;
  std::string device_name;
  std::string device_model;
  std::string device_platform;
  std::string client_sw_version;
  std::string client_sw_build;
  std::string system_sw;
  std::string system_version;
  std::string user_agent;
};

class ATTR_DLL_LOCAL CPVREon : public kodi::addon::CAddonBase,
                                public kodi::addon::CInstancePVRClient
{
public:
  CPVREon();
  ~CPVREon() override;

  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;
  PVR_ERROR GetConnectionString(std::string& connection) override;
  PVR_ERROR GetBackendHostname(std::string& hostname) override;

  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override;
  PVR_ERROR GetDriveSpace(uint64_t& total, uint64_t& used) override;

  PVR_ERROR CallEPGMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                            const kodi::addon::PVREPGTag& item) override;
  PVR_ERROR CallChannelMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                const kodi::addon::PVRChannel& item) override;
  PVR_ERROR CallTimerMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                              const kodi::addon::PVRTimer& item) override;
  PVR_ERROR CallRecordingMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                  const kodi::addon::PVRRecording& item) override;
  PVR_ERROR CallSettingsMenuHook(const kodi::addon::PVRMenuhook& menuhook) override;

  PVR_ERROR GetEPGForChannel(int channelUid,
                             time_t start,
                             time_t end,
                             kodi::addon::PVREPGTagsResultSet& results) override;
  PVR_ERROR IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable) override;
  PVR_ERROR GetEPGTagStreamProperties(
      const kodi::addon::PVREPGTag& tag,
      std::vector<kodi::addon::PVRStreamProperty>& properties) override;
  PVR_ERROR GetProvidersAmount(int& amount) override;
  PVR_ERROR GetProviders(kodi::addon::PVRProvidersResultSet& results) override;
  PVR_ERROR GetChannelGroupsAmount(int& amount) override;
  PVR_ERROR GetChannelGroups(bool bRadio, kodi::addon::PVRChannelGroupsResultSet& results) override;
  PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                   kodi::addon::PVRChannelGroupMembersResultSet& results) override;
  PVR_ERROR GetChannelsAmount(int& amount) override;
  PVR_ERROR GetChannels(bool bRadio, kodi::addon::PVRChannelsResultSet& results) override;
  PVR_ERROR GetRecordingsAmount(bool deleted, int& amount) override;
  PVR_ERROR GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) override;
  PVR_ERROR GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) override;
  PVR_ERROR GetTimersAmount(int& amount) override;
  PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override;
  PVR_ERROR GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus) override;
  PVR_ERROR GetChannelStreamProperties(
      const kodi::addon::PVRChannel& channel,
      std::vector<kodi::addon::PVRStreamProperty>& properties) override;
  PVR_ERROR GetRecordingStreamProperties(
      const kodi::addon::PVRRecording& recording,
      std::vector<kodi::addon::PVRStreamProperty>& properties) override;

  ADDON_STATUS SetSetting(const std::string& settingName,
                        const std::string& settingValue);

protected:
  std::string GetRecordingURL(const kodi::addon::PVRRecording& recording);
  bool GetChannel(const kodi::addon::PVRChannel& channel, EonChannel& myChannel);
  bool GetServer(bool isLive, EonServer& myServer);

private:
  PVR_ERROR CallMenuHook(const kodi::addon::PVRMenuhook& menuhook);

  void SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                           const std::string& url,
                           bool realtime, bool playTimeshiftBuffer,
                           const std::string& license);

  PVR_ERROR GetStreamProperties(
    const EonChannel& channel,
    std::vector<kodi::addon::PVRStreamProperty>& properties, int starttime, bool isLive);

  bool Parametrize(const int id);

  std::vector<EonChannel> m_channels;
  std::vector<EonServer> m_live_servers;
  std::vector<EonServer> m_timeshift_servers;
  std::vector<EonRenderingProfile> m_rendering_profiles;
  std::vector<EonCategory> m_categories;
  std::vector<EonCDN> m_cdns;
/*
  std::string m_drmid;
  std::string m_sessionid;
  std::string m_ipaddress;
  std::string m_deviceid;
*/
//  std::string m_access_token;
//  std::string m_refresh_token;
//  std::string m_token_type;
  std::string m_device_id;
  std::string m_device_number;
  std::string m_device_serial;
  std::string m_subscriber_id;
  std::string m_session_id;
  std::string m_stream_key;
  std::string m_stream_un;
  std::string m_service_provider;
  std::string m_support_web;
//  std::string m_ss_access;
  std::string m_ss_identity;

  std::string m_api;
  std::string m_images_api;

//  std::string m_ss_refresh;
//  int m_active_profile_id;
//  int m_expires_in;

  HttpClient *m_httpClient;
  CSettings* m_settings;
  EonParameters m_parameters;

  std::string GetTime();
  int getBitrate(const bool isRadio, const int id);
  std::string getCoreStreamId(const int id);
  std::string GetBaseApi(const std::string& cdn_identifier);
  std::string GetBrandIdentifier();
  bool GetCDNInfo();
//  bool SelfServiceLogin();
  bool GetDeviceData();
  bool GetDeviceFromSerial();
  bool GetHouseholds();
  bool GetServers();
  bool GetServiceProvider();
  bool GetRenderingProfiles();
  bool LoadChannels(const bool isRadio);
  bool GetCategories(const bool isRadio);
  int GetDefaultNumber(const bool isRadio, int id);
  bool HandleSession(bool start, int cid, int epg_id);
/*
  bool HrtiLogin();
  bool GetIpAddress();
  bool GrantAccess();
  bool RegisterDevice();
  bool LoadChannels();
  bool AuthorizeSession(std::string ref_id, std::string drm_id);
  std::string GetLicense(std::string drm_id, std::string user_id);
*/
//  std::string ltrim(const std::string &s);
//  std::string rtrim(const std::string &s);
//  std::string trim(const std::string &s);
//  std::string urlencode(const std::string &s);
};
