/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <kodi/addon-instance/PVR.h>
#include "Settings.h"
#include "StreamRedirectProxy.h"
#include "http/HttpClient.h"
#include "rapidjson/document.h"

static const int INPUTSTREAM_ADAPTIVE = 0;
static const int INPUTSTREAM_FFMPEGDIRECT = 1;

static const int PLATFORM_WEB = 0;
static const int PLATFORM_ANDROIDTV = 1;

// The backend answers a guide request for several channels at once -- the
// per-channel arrays come back keyed by channel id -- but rejects more ids
// than this in one request with HTTP 400 invalid_input.
static const size_t EPG_CHANNELS_PER_REQUEST = 15;
// Channels in the first prefetch request. Kodi is already blocked on one
// channel when the prefetch starts, and it walks the guide faster than
// full-size batches come back, so the first requests are deliberately small
// and double in size until they reach EPG_CHANNELS_PER_REQUEST.
static const size_t EPG_FIRST_BATCH_CHANNELS = 2;
// Worker threads filling the prefetch. Measured against the live backend over
// a 10 day window for 289 channels: ~37s one channel per request (what Kodi
// asks for on its own), ~22s batched, ~5s batched across four threads, and
// eight threads buy nothing on top of that -- the backend is the limit by then.
static const size_t EPG_PREFETCH_THREADS = 4;
// How many channels may sit fetched-but-not-yet-collected before the workers
// pause. Bounds what the prefetch costs in memory, since Kodi collects the
// channels in its own order rather than the one we fetch them in.
static const size_t EPG_PREFETCH_MAX_READY = 60;
// Only prefetch when Kodi is populating a whole guide window. Short lookups
// (the single programme around a given time) stay plain one-off requests.
static const time_t EPG_PREFETCH_MIN_WINDOW = 12 * 60 * 60;
// Distinct channels that have to ask for the same window before the prefetch
// starts. Kodi also refreshes single channels on their own (a channel that
// came back empty, a manual update request); fanning one of those out into a
// fetch of the entire guide would cost far more than it saves, and a run of
// one-off requests never reaches this many channels.
static const size_t EPG_PREFETCH_TRIGGER_CHANNELS = 3;
// Safety net for a batch that never lands (dead network): the caller falls
// back to fetching its own channel, which then reports the error as before.
static const int EPG_PREFETCH_TIMEOUT_SECONDS = 60;
// How much of the already-aired guide a refresh hands to Kodi again once it
// has been given the full history at least once. Kodi never drops a programme
// because the add-on stopped sending it -- it only ages tags out past
// `pastdaystodisplay` -- so re-sending a week of programmes that have already
// aired and can no longer change is work neither side needs to repeat. A day
// is kept as slack for a schedule that shifted right at the boundary.
static const time_t EPG_INCREMENTAL_PAST = 24 * 60 * 60;

// One programme, holding just the fields the add-on passes on to Kodi.
// Deliberately not a kodi::addon::PVREPGTag: copying one of those copies the
// C struct's char pointers but not their targets, so a PVREPGTag stored in a
// container would be left pointing at freed strings the moment the container
// reallocates.
struct EonEpgEntry
{
  int broadcastId = 0;
  time_t startTime = 0;
  time_t endTime = 0;
  int seriesNumber = 0;
  int episodeNumber = 0;
  int parentalRating = 0;
  unsigned int flags = EPG_TAG_FLAG_UNDEFINED;
  std::string title;
  std::string originalTitle;
  std::string plot;
  std::string iconPath;
};

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
  // A list the viewer built on their account, as opposed to a category the
  // provider publishes; the two come from different endpoints and their ids
  // are separate number spaces.
  bool isCustom = false;
  // The viewer's main list -- only ever set on a custom one.
  bool isPrimary = false;
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
  bool startupTimelineReady = false;
  bool ignoreInitialArchiveSeeks = false;
  EonChannel channel;
  time_t programmeStartTime = 0;
  time_t programmeEndTime = 0;
  time_t sessionStartTime = 0;
  int64_t virtualUnitsPerSecond = 1000;
  int64_t virtualLength = 0;
  int64_t currentPosition = 0;
  int64_t openMonotonicMs = 0;
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
    // Video bitrate of the chosen profile, kept separate from `bitrate`
    // (which is the audio bitrate on radio channels) for minvbr/maxvbr.
    int videoBitrate = 0;
    std::string serverIp;
    std::string serverHostname;
  };

  void SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                           const std::string& url,
                           const bool& realtime,
                           const bool& playTimeshiftBuffer,
                           const bool& isLive,
                           time_t starttime,
                           time_t endtime,
                           bool catchupProxyReady,
                           bool playForwardIndefinitely = false,
                           bool isRadio = false);
  bool BuildPlaybackUrl(const EonChannel& channel,
                        time_t starttime,
                        time_t endtime,
                        const bool& isLive,
                        EonPlaybackUrlResult& result,
                        const bool includeDiagnostics = true);
  bool UseExperimentalNativeStream() const;
  const std::string& GetUserAgent() const;
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
    const bool& isLive,
    bool playForwardIndefinitely = false);

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

  // Currently playing programme, tracked for GetStreamTimes() (progress
  // bar / timeline) on the standard (non-native-stream) playback path.
  time_t m_stream_start_time = 0;
  time_t m_stream_end_time = 0;
  bool m_stream_is_live = false;
  // True when the current live channel is playing through the catchup seek
  // proxy (see SetStreamProperties); false when it fell back to plain
  // stream_mode=timeshift (EPG lookup or proxy start failure). GetStreamTimes()
  // uses this to know whether ffmpegdirect already reports accurate times on
  // its own (catchup) or whether it still needs our own estimate (timeshift).
  bool m_live_using_catchup = false;
  StreamRedirectProxy m_redirectProxy;

  // Offset between the backend's clock and this device's, in milliseconds
  // (backend - device), and when it was last measured. Stream URLs embed a
  // ctime the CDN wants within ~20s of real time, which an offset satisfies
  // just as well as a fresh reading -- see GetTime().
  int64_t m_server_time_offset_ms = 0;
  int64_t m_server_time_synced_at_ms = 0;
  bool m_server_time_synced = false;

  // Last known airtime of the programme currently on each channel, keyed by
  // channel uid. Valid by construction only while that programme is still
  // running, which is exactly when tuning in wants it -- see
  // GetChannelStreamProperties().
  struct EonAiringProgramme
  {
    time_t startTime = 0;
    time_t endTime = 0;
  };
  std::map<int, EonAiringProgramme> m_airing_programmes;

  // Kodi asks for the guide one channel at a time and waits for each answer,
  // so populating every channel is one blocking round trip per channel. The
  // first call of an update pass instead starts a background prefetch that
  // pulls the whole guide in batched, parallel requests; every call is then
  // served out of `ready` -- see GetEPGForChannel().
  struct EonEpgPrefetch
  {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::thread> workers;
    std::deque<std::vector<int>> queue; // channel uids, batched, not fetched yet
    std::set<int> pending; // uids this prefetch still owes an answer for
    std::map<int, std::vector<EonEpgEntry>> ready; // fetched, by channel uid
    std::set<int> failed; // batch failed; the caller retries that channel alone
    time_t start = 0;
    time_t end = 0;
    // Channels seen asking for `observedStart`..`observedEnd` so far, which is
    // what tells a full update pass apart from a one-off single-channel
    // refresh -- see EPG_PREFETCH_TRIGGER_CHANNELS.
    std::set<int> observed;
    time_t observedStart = 0;
    time_t observedEnd = 0;
    size_t waiters = 0;
    // Batches still to land, and what they have brought in so far, so the
    // worker that finishes the last one can report what the whole refresh
    // cost. Without that the log shows a prefetch starting and never says
    // when -- or whether -- it got there.
    std::chrono::steady_clock::time_point startedAt;
    size_t batchesLeft = 0;
    size_t entriesFetched = 0;
    // Bumped every time a prefetch is (re)started. A caller can be waiting on
    // a channel while a new update pass replaces the prefetch underneath it;
    // comparing generations is how it notices, rather than being handed
    // programmes fetched for a window it never asked about.
    uint64_t generation = 0;
    bool running = false;
    bool stop = false;
    // Whether this pass is handing Kodi the whole window Kodi asked for. Only
    // a full one may be recorded as having delivered the history.
    bool fullWindow = false;
  };
  EonEpgPrefetch m_epgPrefetch;
  // Serialises starting and stopping the prefetch as a whole, so two update
  // passes arriving together cannot both tear down and rebuild it.
  std::mutex m_epgPrefetchLifecycle;

  // When the add-on last handed Kodi the full guide window, and for how many
  // channels. Persisted, because the refresh that matters is the one right
  // after Kodi starts -- exactly when in-memory state is gone. A different
  // channel count means channels whose history Kodi cannot have, so that
  // forces a full window too.
  time_t m_epgFullPassAt = 0;
  size_t m_epgFullPassChannels = 0;
  // How much history that pass carried, so that raising `pastdaystodisplay`
  // is not mistaken for history Kodi already has.
  time_t m_epgFullPassPast = 0;
  // The window Kodi is currently asking for and the start we resolved for it.
  // Held so every channel of a pass is fetched and served over one window --
  // recomputing per channel would drift and leave the prefetch unusable.
  std::mutex m_epgWindowMutex;
  time_t m_epgWindowStart = 0;
  time_t m_epgWindowEnd = 0;
  time_t m_epgFetchStart = 0;
  // Whether the resolved window delivers everything obtainable of what Kodi
  // asked for (an archive-depth clamp still counts; an incremental trim does
  // not). Only a full pass may be recorded as having delivered the history.
  bool m_epgWindowFullDelivery = true;

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
  void LoadEpgDeliveryState();
  void SaveEpgDeliveryState();
  time_t EpgFetchStart(time_t start, time_t end, bool& fullWindow);
  EonEpgEntry ParseEpgEntry(const rapidjson::Value& epgItem) const;
  bool FetchEpgEntries(const std::vector<int>& channelUids,
                       time_t start,
                       time_t end,
                       std::map<int, std::vector<EonEpgEntry>>& entries);
  void StartEpgPrefetch(int firstChannelUid, time_t start, time_t end, bool fullWindow);
  void StopEpgPrefetch();
  void EpgPrefetchWorker();
  bool TakePrefetchedEpg(int channelUid,
                         time_t start,
                         time_t end,
                         std::vector<EonEpgEntry>& entries);
  int getVideoBitrate(const int id);
  bool GetPostJson(const std::string& url, const std::string& body, rapidjson::Document& doc,
                    bool showErrorDialog = true);
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
  void SelectAccountProfile();
  void LoadCustomChannelLists();
  void OrderChannelGroups();
  bool RefreshDeviceRegistration();
  int GetDefaultNumber(const bool isRadio, int id);
  bool HandleSession(bool start, int cid, int epg_id);

  // NEW: v2-first / v1-fallback device registration.
  struct DeviceRegistration
  {
    std::string deviceId;
    std::string deviceNumber;
  };

  bool RegisterDevice(const std::string& v2Body,
                      const std::string& v1Body,
                      DeviceRegistration& out);
  std::string BuildV1DeviceBody() const;
  std::string BuildV2DeviceBody(const std::string& deviceNumber) const;
};
