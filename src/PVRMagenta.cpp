/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "PVRHrti.h"

#include <algorithm>

#include <kodi/General.h>
//#include <tinyxml2.h>
#include "Utils.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "Base64.h"

//using namespace tinyxml2;
//using namespace rapidjson;

/***********************************************************
  * PVR Client AddOn specific public library functions
  ***********************************************************/


std::string ltrim(const std::string &s)
{
    size_t start = s.find_first_not_of("\"");
    return (start == std::string::npos) ? "" : s.substr(start);
}

std::string rtrim(const std::string &s)
{
    size_t end = s.find_last_not_of("\"");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

std::string trim(const std::string &s) {
    return rtrim(ltrim(s));
}

bool CPVRHrti::GetIpAddress()
{
  std::string jsonString;
  int statusCode = 0;

  kodi::Log(ADDON_LOG_DEBUG, "Get IP Address");

  jsonString = m_httpClient->HttpGet(HRTI_API + "getIPAddress", statusCode);

  m_ipaddress = trim(jsonString);
  kodi::Log(ADDON_LOG_DEBUG, "GetIpAddress returned: %s", m_ipaddress.c_str());

  return true;
}

bool CPVRHrti::GrantAccess()
{
  std::string jsonString;
  int statusCode = 0;

  kodi::Log(ADDON_LOG_DEBUG, "Grant Access");

  std::string username = m_settings->GetHrtiUsername();
  std::string password = m_settings->GetHrtiPassword();

  if (username.empty() || password.empty()) {
    return false;
  }

  std::string postData = "{\"Username\": \"" + username
                          + "\", \"Password\": \"" + password
                          + "\", \"OperatorReferenceId\": \"hrt\"}";

  jsonString = m_httpClient->HttpPost(HRTI_API + "GrantAccess", postData, statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError() || !doc.HasMember("ErrorCode") || !doc.HasMember("ErrorDescription") || !doc.HasMember("Result"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to grant access");
    return false;
  }

  int errorcode = Utils::JsonIntOrZero(doc, "ErrorCode");
  if (errorcode != 0) {
    std::string errordesc = Utils::JsonStringOrEmpty(doc, "ErrorDescription");
    kodi::Log(ADDON_LOG_ERROR, "Failed to grant access");
    kodi::Log(ADDON_LOG_DEBUG, "Error Code: / Error Description: %s", errordesc.c_str());
    return false;
  }

  kodi::Log(ADDON_LOG_DEBUG, "GrantAccess returned: %s", jsonString.c_str());

  const rapidjson::Value& result = doc["Result"];
  m_token = result["Token"].GetString();

  kodi::Log(ADDON_LOG_DEBUG, "Token returned: %s", m_token.c_str());

  return true;
}

bool CPVRHrti::RegisterDevice()
{
  std::string jsonString;
  int statusCode = 0;
  bool returnvalue  = false;
  std::string postData = "{}";

  jsonString = m_httpClient->HttpPost(AVIION_API + "DeviceInstancesGet", postData, statusCode);

  kodi::Log(ADDON_LOG_DEBUG, "Devices: %s", jsonString.c_str());

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError() || !doc.HasMember("ErrorCode") || !doc.HasMember("ErrorDescription") || !doc.HasMember("Result"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get device instances");
    return returnvalue;
  }

  int errorcode = Utils::JsonIntOrZero(doc, "ErrorCode");
  if (errorcode != 0) {
    std::string errordesc = Utils::JsonStringOrEmpty(doc, "ErrorDescription");
    kodi::Log(ADDON_LOG_ERROR, "Failed to get device instances");
    kodi::Log(ADDON_LOG_DEBUG, "Error Code: / Error Description: %s", errordesc.c_str());
    return returnvalue;
  }

  int i = 0;
  const rapidjson::Value& devices = doc["Result"];
  bool ipmatched = false;
  std::string deviceipaddress = "";
  std::string deviceserial = "";

  for (rapidjson::Value::ConstValueIterator itr1 = devices.Begin();
      itr1 != devices.End(); ++itr1)
  {
    const rapidjson::Value& deviceitem = (*itr1);
    deviceserial = Utils::JsonStringOrEmpty(deviceitem, "Serial");
    kodi::Log(ADDON_LOG_DEBUG, "Device Serial: %s", deviceserial.c_str());
    if (deviceserial != "" && !ipmatched) {
      m_deviceid = deviceserial;
      deviceipaddress = Utils::JsonStringOrEmpty(deviceitem, "IpAddress");
      if (deviceipaddress == m_settings->GetHrtiIpAddress()) {
        ipmatched = true;
        return true;
      }
    }
  }
  if (deviceserial != "") {
    return true;
  }
  else {
    m_deviceid = Utils::CreateUUID();

    statusCode = 0;
    postData = "{\"DeviceSerial\": \"" + m_deviceid
                + "\", \"DeviceReferenceId\": \"6"
                + "\", \"IpAddress\": \"" + m_ipaddress
                + "\", \"ConnectionType\": \"LAN/WiFi"
                + "\", \"ApplicationVersion\": \"5.62.5"
                + "\", \"DrmId\": \"" + m_deviceid
                + "\", \"OsVersion\": \"Linux"
                + "\", \"ClientType\": \"Chrome 96\"}";

    jsonString = m_httpClient->HttpPost(AVIION_API + "RegisterDevice", postData, statusCode);

    return true;
  }
  return false;
}

CPVRHrti::CPVRHrti() :
  m_settings(new CSettings())
{
  m_settings->Load();
  m_httpClient = new HttpClient(m_settings);

  m_ipaddress = m_settings->GetHrtiIpAddress();
  if (m_ipaddress.empty()) {
    if (GetIpAddress()) {
      SetSetting("ipaddress", m_ipaddress);
    }
  }
  m_token = m_settings->GetHrtiToken();
  if (m_token.empty()) {
    if (GrantAccess()) {
      SetSetting("token", m_token);
    }
  }
  m_deviceid = m_settings->GetHrtiDeviceID();
  if (m_deviceid.empty()) {
    if (RegisterDevice()) {
      SetSetting("deviceid", m_deviceid);
    }
  }

  LoadChannels();

  AddMenuHook(kodi::addon::PVRMenuhook(1, 30000, PVR_MENUHOOK_SETTING));
  AddMenuHook(kodi::addon::PVRMenuhook(2, 30001, PVR_MENUHOOK_ALL));
  AddMenuHook(kodi::addon::PVRMenuhook(3, 30002, PVR_MENUHOOK_CHANNEL));
  AddMenuHook(kodi::addon::PVRMenuhook(4, 30002, PVR_MENUHOOK_EPG));
}

CPVRHrti::~CPVRHrti()
{
  m_channels.clear();
}

ADDON_STATUS CPVRHrti::SetSetting(const std::string& settingName, const std::string& settingValue)
{
  ADDON_STATUS result = m_settings->SetSetting(settingName, settingValue);
  if (!m_settings->VerifySettings()) {
    return ADDON_STATUS_NEED_SETTINGS;
  }
  return result;
}

bool CPVRHrti::LoadChannels()
{
  kodi::Log(ADDON_LOG_DEBUG, "Load Hrti Channels");
  std::string jsonString;
  int statusCode = 0;
  std::string postData = "{}";

  jsonString = m_httpClient->HttpPost(HRTI_API + "GetChannels", postData, statusCode);

  kodi::Log(ADDON_LOG_DEBUG, "ContentStr: %s", jsonString.c_str());

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError() || !doc.HasMember("ErrorCode") || !doc.HasMember("ErrorDescription") || !doc.HasMember("Result"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to load channels");
    return false;
  }

  int errorcode = Utils::JsonIntOrZero(doc, "ErrorCode");
  if (errorcode != 0) {
    std::string errordesc = Utils::JsonStringOrEmpty(doc, "ErrorDescription");
    kodi::Log(ADDON_LOG_ERROR, "Failed to load channels");
    kodi::Log(ADDON_LOG_DEBUG, "Error Code: / Error Description: %s", errordesc.c_str());
    return false;
  }

  int i = 0;
  const rapidjson::Value& channels = doc["Result"];

  for (rapidjson::Value::ConstValueIterator itr1 = channels.Begin();
      itr1 != channels.End(); ++itr1)
  {
    const rapidjson::Value& channelItem = (*itr1);

    std::string channame = Utils::JsonStringOrEmpty(channelItem, "Name");
    kodi::Log(ADDON_LOG_DEBUG, "Channel Name: %s", channame.c_str());

    HrtiChannel hrti_channel;

    hrti_channel.iChannelNumber = ++i;
    hrti_channel.bRadio = Utils::JsonBoolOrFalse(channelItem, "Radio");
    hrti_channel.bArchive = Utils::JsonBoolOrFalse(channelItem, "RestartTV");
    hrti_channel.strChannelName = Utils::JsonStringOrEmpty(channelItem, "Name");
    std::string ref_id = Utils::JsonStringOrEmpty(channelItem, "ReferenceID");
    hrti_channel.referenceID = ref_id;
    hrti_channel.strIconPath = Utils::JsonStringOrEmpty(channelItem, "Icon");
    hrti_channel.iUniqueId = Utils::GetChannelId(ref_id.c_str());
    hrti_channel.strStreamURL = Utils::JsonStringOrEmpty(channelItem, "StreamingURL");

    m_channels.emplace_back(hrti_channel);

  }

//  kodi::Log(ADDON_LOG_DEBUG, "Channels: %s", jsonString);

  //HttpRequest(curl, "POST", "https://auth.waipu.tv/oauth/token", postData, statusCode);
  return true;
}

std::string CPVRHrti::GetLicense(std::string drm_id, std::string user_id)
{
  std::string license_plain = "{\"userId\": \"" + user_id
                            + "\", \"sessionId\": \"" + drm_id
                            + "\", \"merchant\": \"" + MY_MERCHANT + "\"}";

  kodi::Log(ADDON_LOG_DEBUG, "[jwt] license_plain: %s", license_plain.c_str());
  std::string license = base64_encode(license_plain.c_str(), license_plain.length());
  kodi::Log(ADDON_LOG_DEBUG, "[jwt] license: %s", license.c_str());


  return license;
}

bool CPVRHrti::AuthorizeSession(std::string ref_id, std::string drm_id)
{
  std::string jsonString;
  int statusCode = 0;

  std::string postData = "{\"ContentType\": \"tlive\", \"ContentReferenceId\": \"" + ref_id
                          + "\", \"ContentDrmId\": \"" + drm_id
                          +"\", \"VideostoreReferenceIds\": null"
                          +", \"ChannelReferenceId\": \"" + ref_id
                          +"\", \"StartTime\": null"
                          +", \"EndTime\": null}";

  kodi::Log(ADDON_LOG_DEBUG, "Authorize Session: %s", postData.c_str());

  jsonString = m_httpClient->HttpPost(HRTI_API + "AuthorizeSession", postData, statusCode);
  kodi::Log(ADDON_LOG_DEBUG, "Authorize Session returned: %s", jsonString.c_str());

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError() || !doc.HasMember("Result"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to authorize session");
    return false;
  }

//  std::string errorcode = doc["ErrorCode"].GetString();
  std::string errordesc = Utils::JsonStringOrEmpty(doc, "ErrorDescription");;
  kodi::Log(ADDON_LOG_DEBUG, "Error Code: / Error Description: %s", errordesc.c_str());

  const rapidjson::Value& result = doc["Result"];
  m_drmid = Utils::JsonStringOrEmpty(result, "DrmId");
  m_sessionid = Utils::JsonStringOrEmpty(result, "SessionId");
  kodi::Log(ADDON_LOG_DEBUG, "DrmID: %s SessionID: %s", m_drmid.c_str(), m_sessionid.c_str());

  return true;
}

void CPVRHrti::SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                                    const std::string& url,
                                    bool realtime, bool playTimeshiftBuffer,
                                    const std::string& license)
{
  kodi::Log(ADDON_LOG_DEBUG, "[PLAY STREAM] url: %s", url.c_str());

  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, url);
  properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
  properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, realtime ? "true" : "false");

  // MPEG DASH
  kodi::Log(ADDON_LOG_DEBUG, "[PLAY STREAM] dash");
  properties.emplace_back("inputstream.adaptive.manifest_type", "mpd");
  properties.emplace_back(PVR_STREAM_PROPERTY_MIMETYPE, "application/xml+dash");

  properties.emplace_back("inputstream.adaptive.license_type", "com.widevine.alpha");
  properties.emplace_back("inputstream.adaptive.license_key",
                          "https://lic.drmtoday.com/license-proxy-widevine/cenc/"
                          "|Content-Type=text%2Fxml&dt-custom-data=" +
                              license + "|R{SSM}|JBlicense");

  properties.emplace_back("inputstream.adaptive.manifest_update_parameter", "full");
}


PVR_ERROR CPVRHrti::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(true);
  capabilities.SetSupportsChannelGroups(false);
  capabilities.SetSupportsRecordings(false);
  capabilities.SetSupportsRecordingsDelete(false);
  capabilities.SetSupportsRecordingsUndelete(false);
  capabilities.SetSupportsTimers(false);
  capabilities.SetSupportsRecordingsRename(false);
  capabilities.SetSupportsRecordingsLifetimeChange(false);
  capabilities.SetSupportsDescrambleInfo(false);
  capabilities.SetSupportsProviders(false);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetBackendName(std::string& name)
{
  name = "hrti pvr add-on";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetBackendVersion(std::string& version)
{
  version = "0.1";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetConnectionString(std::string& connection)
{
  connection = "connected";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetBackendHostname(std::string& hostname)
{
  hostname = "";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  total = 1024 * 1024 * 1024;
  used = 0;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetEPGForChannel(int channelUid,
                                     time_t start,
                                     time_t end,
                                     kodi::addon::PVREPGTagsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  for (const auto& channel : m_channels)
  {

    if (channel.iUniqueId != channelUid)
      continue;

    std::string startTime = Utils::TimeToString(start);
    std::string endTime = Utils::TimeToString(end);

    std::string jsonEpg;
    int statusCode = 0;

    kodi::Log(ADDON_LOG_DEBUG, "Start %u End %u", start, end);
    kodi::Log(ADDON_LOG_DEBUG, "EPG Request for channel x from %s to %s", startTime.c_str(), endTime.c_str());

    std::string postData = "{\"ChannelReferenceIds\": [\"" + channel.referenceID
                           + "\"], \"StartTime\": \"/Date(" + std::to_string(start * 1000)
                           + ")/\", \"EndTime\": \"/Date(" + std::to_string(end * 1000) + ")/\"}";

    kodi::Log(ADDON_LOG_DEBUG, "PostData %s", postData.c_str());
    jsonEpg = m_httpClient->HttpPost(HRTI_API + "GetProgramme", postData, statusCode);
    kodi::Log(ADDON_LOG_DEBUG, "GetProgramme returned: %s", jsonEpg.c_str());

    rapidjson::Document epgDoc;
    epgDoc.Parse(jsonEpg.c_str());
    if (epgDoc.GetParseError())
    {
      kodi::Log(ADDON_LOG_ERROR, "[GetEPG] ERROR: error while parsing json");
      return PVR_ERROR_SERVER_ERROR;
    }
    kodi::Log(ADDON_LOG_DEBUG, "[epg] iterate entries");

    for (const auto& epgChannel : epgDoc["Result"].GetArray())
    {
      for (const auto& epgList : epgChannel["EpgList"].GetArray())
      {
        kodi::addon::PVREPGTag tag;

        tag.SetUniqueBroadcastId(std::stoi(Utils::JsonStringOrEmpty(epgList,"ReferenceID")));
        tag.SetUniqueChannelId(channel.iUniqueId);
        tag.SetTitle(Utils::JsonStringOrEmpty(epgList,"Title"));

        tag.SetStartTime(Utils::JsonIntOrZero(epgList,"TimeStartUnixEpoch"));
        tag.SetEndTime(Utils::JsonIntOrZero(epgList,"TimeEndUnixEpoch"));

        tag.SetIconPath(Utils::JsonStringOrEmpty(epgList,"ImagePath"));

        results.Add(tag);
      }
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable)
{
  bIsPlayable = false;

  for (const auto& channel : m_channels)
  {
    if (channel.iUniqueId == tag.GetUniqueChannelId())
    {
      bIsPlayable = channel.bArchive;
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetEPGTagStreamProperties(
    const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetProvidersAmount(int& amount)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetProviders(kodi::addon::PVRProvidersResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetChannelsAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  amount = m_channels.size();
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Channels Amount: [%s]", amount_str.c_str());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetChannels(bool bRadio, kodi::addon::PVRChannelsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  for (const auto& channel : m_channels)
  {
    if (channel.bRadio == bRadio)
    {
      kodi::addon::PVRChannel kodiChannel;

      kodiChannel.SetUniqueId(channel.iUniqueId);
      kodiChannel.SetIsRadio(channel.bRadio);
      kodiChannel.SetChannelNumber(channel.iChannelNumber);
//      kodiChannel.SetSubChannelNumber(channel.iSubChannelNumber);
      kodiChannel.SetChannelName(channel.strChannelName);
//      kodiChannel.SetEncryptionSystem(channel.iEncryptionSystem);
      kodiChannel.SetIconPath(channel.strIconPath);
      kodiChannel.SetIsHidden(false);
      kodiChannel.SetHasArchive(channel.bArchive);

      /* PVR API 8.0.0 */
//      kodiChannel.SetClientProviderUid(channel.iProviderId);

      results.Add(kodiChannel);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetChannelStreamProperties(
    const kodi::addon::PVRChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  HrtiChannel addonChannel;
  GetChannel(channel, addonChannel);

  kodi::Log(ADDON_LOG_DEBUG, "Stream URL -> %s", addonChannel.strStreamURL.c_str());
  kodi::Log(ADDON_LOG_DEBUG, "ReferenceID -> %s", addonChannel.referenceID.c_str());

  std::string parse_str = addonChannel.strStreamURL;
  std::string delimiter = "://";
  std::string drm_str;
  size_t pos = 0;
  int i = 0;
  pos = parse_str.find(delimiter);
  parse_str.erase(0, pos + delimiter.length());
  delimiter = "/";
  while ((pos = parse_str.find(delimiter)) != std::string::npos) {
    if (i == 1)
    {
      drm_str = parse_str.substr(0, pos) + "_";
    }
    if (i == 2)
    {
      drm_str += parse_str.substr(0, pos);
    }
    parse_str.erase(0, pos + delimiter.length());
    i++;
  }

  bool authorized = AuthorizeSession(addonChannel.referenceID, drm_str);

  delimiter = ":";

  pos = 0;
  i = 0;
  std::string user_id;
  std::string session_str = m_sessionid;
  while ((pos = session_str.find(delimiter)) != std::string::npos) {
    if (i == 2)
    {
      user_id = session_str.substr(0, pos);
    }
    session_str.erase(0, pos + delimiter.length());
    i++;
  }

  std::string license = GetLicense(m_drmid, user_id);
  SetStreamProperties(properties, addonChannel.strStreamURL, true, false, license);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetChannelGroupsAmount(int& amount)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetChannelGroups(bool bRadio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                           kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus)
{
  signalStatus.SetAdapterName("pvr hrti backend");
  signalStatus.SetAdapterStatus("OK");

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetRecordingsAmount(bool deleted, int& amount)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetRecordingStreamProperties(
    const kodi::addon::PVRRecording& recording,
    std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  /* TODO: Implement this to get support for the timer features introduced with PVR API 1.9.7 */
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR CPVRHrti::GetTimersAmount(int& amount)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRHrti::CallEPGMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                    const kodi::addon::PVREPGTag& item)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);


  return CallMenuHook(menuhook);
}

PVR_ERROR CPVRHrti::CallChannelMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                        const kodi::addon::PVRChannel& item)
{
  return CallMenuHook(menuhook);
}

PVR_ERROR CPVRHrti::CallTimerMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                      const kodi::addon::PVRTimer& item)
{
  return CallMenuHook(menuhook);
}

PVR_ERROR CPVRHrti::CallRecordingMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                          const kodi::addon::PVRRecording& item)
{
  return CallMenuHook(menuhook);
}

PVR_ERROR CPVRHrti::CallSettingsMenuHook(const kodi::addon::PVRMenuhook& menuhook)
{
  return CallMenuHook(menuhook);
}

PVR_ERROR CPVRHrti::CallMenuHook(const kodi::addon::PVRMenuhook& menuhook)
{
  int iMsg;
  switch (menuhook.GetHookId())
  {
    case 1:
      iMsg = 30010;
      break;
    case 2:
      iMsg = 30011;
      break;
    case 3:
      iMsg = 30012;
      break;
    case 4:
      iMsg = 30012;
      break;
    default:
      return PVR_ERROR_INVALID_PARAMETERS;
  }
  kodi::QueueNotification(QUEUE_INFO, "", kodi::addon::GetLocalizedString(iMsg));

  return PVR_ERROR_NO_ERROR;
}

bool CPVRHrti::GetChannel(const kodi::addon::PVRChannel& channel, HrtiChannel& myChannel)
{
  for (const auto& thisChannel : m_channels)
  {
    if (thisChannel.iUniqueId == (int)channel.GetUniqueId())
    {
      myChannel.iUniqueId = thisChannel.iUniqueId;
      myChannel.bRadio = thisChannel.bRadio;
      myChannel.iChannelNumber = thisChannel.iChannelNumber;
//      myChannel.iSubChannelNumber = thisChannel.iSubChannelNumber;
//      myChannel.iEncryptionSystem = thisChannel.iEncryptionSystem;
      myChannel.referenceID = thisChannel.referenceID;
      myChannel.strChannelName = thisChannel.strChannelName;
      myChannel.strIconPath = thisChannel.strIconPath;
      myChannel.strStreamURL = thisChannel.strStreamURL;

      return true;
    }
  }

  return false;
}

std::string CPVRHrti::GetRecordingURL(const kodi::addon::PVRRecording& recording)
{
  return "";
}

bool CPVRHrti::HrtiLogin()
{
  kodi::Log(ADDON_LOG_DEBUG, "[login check] HRTi LOGIN...");

  time_t currTime;
  time(&currTime);
  kodi::Log(ADDON_LOG_DEBUG, "[token] current time %i", currTime);

  return true;
}

ADDONCREATOR(CPVRHrti)
