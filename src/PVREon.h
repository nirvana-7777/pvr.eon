/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include <string>
#include <deque>
#include <vector>

#include <kodi/addon-instance/PVR.h>
#include "Settings.h"
#include "http/HttpClient.h"
#include "rapidjson/document.h"

static const int INPUTSTREAM_ADAPTIVE = 0;
static const int INPUTSTREAM_FFMPEGDIRECT = 1;

static const int PLATFORM_WEB = 0;
static const int PLATFORM_ANDROIDTV = 1;

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
  int ageRating;
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

struct EonPendingPlayback
{
  bool active = false;
  bool liveEdge = false;
  int channelUid = 0;
  time_t startTime = 0;
  time_t endTime = 0;
  time_t initialPlaybackTime = 0;
  time_t requestTime = 0;
};

struct EonNativeStreamState
{
  bool open = false;
  bool isLive = true;
  bool seekable = false;
  bool liveEdge = false;
  EonChannel channel;
  time_t programmeStartTime = 0;
  time_t programmeEndTime = 0;
  time_t sessionStartTime = 0;
  int64_t virtualUnitsPerSecond = 1000;
  int64_t virtualLength = 0;
  int64_t currentPosition = 0;
  int64_t sessionAnchorMonotonicMs = 0;
  int bitrate = 0;
  std::string masterUrl;
  std::string variantUrl;
  std::deque<std::string> pendingFragments;
  std::string lastFragmentUrl;
  std::vector<uint8_t> currentFragmentData;
  size_t currentFragmentOffset = 0;
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
  bool OpenLiveStream(const kodi::addon::PVRChannel& channel) override;
  void CloseLiveStream() override;
  int ReadLiveStream(unsigned char* buffer, unsigned int size) override;
  int64_t SeekLiveStream(int64_t position, int whence) override;
  int64_t LengthLiveStream() override;
  bool CanPauseStream() override;
  bool CanSeekStream() override;
  bool IsRealTimeStream() override;
  PVR_ERROR GetStreamTimes(kodi::addon::PVRStreamTimes& times) override;

  ADDON_STATUS SetSetting(const std::string& settingName,
                        const std::string& settingValue);

protected:
  std::string GetRecordingURL(const kodi::addon::PVRRecording& recording);
  bool GetChannel(const kodi::addon::PVRChannel& channel, EonChannel& myChannel);
  bool GetServer(bool isLive, EonServer& myServer);

private:
  struct EonPlaybackUrlResult
  {
    std::string url;
    std::string streamProfile;
    int bitrate = 0;
  };

  void SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                           const std::string& url,
                           const bool& realtime,
                           const bool& playTimeshiftBuffer,
                           const bool& isLive,
                           time_t starttime,
                           time_t endtime);
  bool BuildPlaybackUrl(const EonChannel& channel,
                        time_t starttime,
                        time_t endtime,
                        const bool& isLive,
                        EonPlaybackUrlResult& result,
                        const bool includeDiagnostics = true);
  bool UseExperimentalNativeStream() const;
  bool OpenNativeStream(const EonChannel& channel,
                        bool isLive,
                        time_t starttime,
                        time_t endtime,
                        time_t initialPlaybackTime = 0,
                        bool liveEdge = false);
  void CloseNativeStreamInternal();
  bool RestartNativeStreamAt(time_t starttime);
  bool UpdateNativeVariantUrl(bool logErrors = true);
  bool PollNativeFragmentQueue(bool forceRefresh = false);
  bool LoadNextNativeFragment();
  bool FetchBinaryUrl(const std::string& url, std::vector<uint8_t>& data, int& statusCode);
  int64_t GetCurrentNativePosition() const;
  time_t GetCurrentNativeSeekableEndTime() const;
  time_t StreamPositionToTime(int64_t position) const;
  int64_t TimeToStreamPosition(time_t timeValue) const;

  PVR_ERROR GetStreamProperties(
    const EonChannel& channel,
    std::vector<kodi::addon::PVRStreamProperty>& properties,
    time_t starttime,
    time_t endtime,
    const bool& isLive);

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
  int m_platform;
  EonPendingPlayback m_pendingPlayback;
  EonNativeStreamState m_nativeStream;

//  std::string m_ss_refresh;
//  int m_active_profile_id;
//  int m_expires_in;

  HttpClient *m_httpClient;
  CSettings* m_settings;

  std::string GetTime();
  int getBitrate(const bool isRadio, const int id);
  bool GetPostJson(const std::string& url, const std::string& body, rapidjson::Document& doc);
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
  bool RefreshDeviceRegistration();
  int GetDefaultNumber(const bool isRadio, int id);
  bool HandleSession(bool start, int cid, int epg_id);
};
