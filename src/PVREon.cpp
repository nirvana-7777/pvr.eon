/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "PVREon.h"
#include "Globals.h"

#include <algorithm>

#include <kodi/General.h>
#include "Utils.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <botan/auto_rng.h>
#include <botan/cipher_mode.h>
#include <botan/hex.h>
#include <botan/rng.h>
#include <botan/base64.h>

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

std::string urlsafeencode(const std::string &s) {
  std::string t = s;
  std::replace( t.begin(), t.end(), '+', '-'); // replace all '+' to '-'
  std::replace( t.begin(), t.end(), '/', '_'); // replace all '/' to '_'
  t.erase(std::remove( t.begin(), t.end(), '='),
              t.end()); // remove padding
  return t;
}

std::string urlsafedecode(const std::string &s) {
  std::string t = s;
  std::replace( t.begin(), t.end(), '-', '+'); // replace all '-' to '+'
  std::replace( t.begin(), t.end(), '_', '/'); // replace all '_' to '/'
//  t.erase(std::remove( t.begin(), t.end(), '='),
//              t.end()); // remove padding
  return t;
}

int CPVREon::getBitrate(const bool isRadio, const int id) {
  for(unsigned int i = 0; i < m_rendering_profiles.size(); i++)
  {
    if (id == m_rendering_profiles[i].id) {
      if (isRadio) {
        return m_rendering_profiles[i].audioBitrate;
      } else {
        return m_rendering_profiles[i].videoBitrate;
      }
    }
  }
  return 0;
}

std::string CPVREon::getCoreStreamId(const int id) {
  for(unsigned int i = 0; i < m_rendering_profiles.size(); i++)
  {
    if (id == m_rendering_profiles[i].id) {
        return m_rendering_profiles[i].coreStreamId;
    }
  }
  return "";
}

int CPVREon::GetDefaultNumber(const bool isRadio, int id) {
  for(unsigned int i = 0; i < m_categories.size(); i++)
  {
    if (m_categories[i].isDefault && m_categories[i].isRadio == isRadio) {
      for(unsigned int j = 0; j < m_categories[i].channels.size(); j++) {
        if (m_categories[i].channels[j].id == id) {
          return m_categories[i].channels[j].position;
        }
      }
    }
  }
  return 0;
}

std::string CPVREon::GetBaseApi(const std::string& cdn_identifier) {

  for(int i=0; i < m_cdns.size(); i++){
    if (m_cdns[i].identifier == cdn_identifier) {
      return m_cdns[i].baseApi;
/*
      for (int j=0; j < m_cdns[i].domains.size(); j++){
        if (m_cdns[i].domains[j].name == "baseApi") {
          return m_cdns[i].domains[j].be;
        }
      }
*/
    }
  }

  return "";
}

std::string CPVREon::GetBrandIdentifier()
{
  std::string jsonString;
  int statusCode = 0;

  jsonString = m_httpClient->HttpGet(BROKER_URL + "v2/brands", statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get brands");
    return "";
  }

  int i = 0;
  int sp_id = m_settings->GetEonServiceProvider();
  kodi::Log(ADDON_LOG_DEBUG, "Requested Service Provider ID:%u", sp_id);

  const rapidjson::Value& brands = doc;

  for (rapidjson::Value::ConstValueIterator itr1 = brands.Begin();
      itr1 != brands.End(); ++itr1)
  {
    if (i == sp_id) {
      const rapidjson::Value& brandItem = (*itr1);
      return Utils::JsonStringOrEmpty(brandItem, "cdnIdentifier");
    }
    i++;
  }

  return "";
}

bool CPVREon::GetCDNInfo()
{
  std::string jsonString;
  int statusCode = 0;

  jsonString = m_httpClient->HttpGet(BROKER_URL + "v1/cdninfo", statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get cdninfo");
    return false;
  }
  const rapidjson::Value& cdns = doc;

  for (rapidjson::Value::ConstValueIterator itr1 = cdns.Begin();
      itr1 != cdns.End(); ++itr1)
  {
    const rapidjson::Value& cdnItem = (*itr1);

    EonCDN cdn;

    cdn.id = Utils::JsonIntOrZero(cdnItem, "id");
    cdn.identifier = Utils::JsonStringOrEmpty(cdnItem, "identifier");
    cdn.isDefault = Utils::JsonBoolOrFalse(cdnItem, "isDefault");

    const rapidjson::Value& baseApi = cdnItem["domains"]["baseApi"];

    cdn.baseApi = Utils::JsonStringOrEmpty(baseApi, "be");
    m_cdns.emplace_back(cdn);
  }

  return true;
}

bool CPVREon::GetDeviceData()
{
  std::string url = SS_PORTAL + "/gateway/SelfCareAPI/1.0/selfcareapi/" + SS_DOMAIN + "/subscriber/" + m_settings->GetSSIdentity() + "/devices/eon/2/product";

  std::string jsonString;
  int statusCode = 0;

  jsonString = m_httpClient->HttpGet(url, statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get self service product data");
    return false;
  }

  std::string friendly_id;
  const rapidjson::Value& devices = doc["devices"];
  for (rapidjson::Value::ConstValueIterator itr1 = devices.Begin();
      itr1 != devices.End(); ++itr1)
  {
    const rapidjson::Value& device = (*itr1);
    m_device_serial = Utils::JsonStringOrEmpty(device, "serialNumber");
    m_device_id = std::to_string(Utils::JsonIntOrZero(device, "id"));
    m_device_number = Utils::JsonStringOrEmpty(device, "deviceNumber");
    friendly_id = Utils::JsonStringOrEmpty(device, "friendlyId");
    size_t found = friendly_id.find("web");
    if (found != std::string::npos) {
        kodi::Log(ADDON_LOG_DEBUG, "Got Device Serial Number: %s, DeviceID: %s, Device Number: %s", m_device_serial.c_str(), m_device_id.c_str(), m_device_number.c_str());
        return true;
    }
  }

  kodi::Log(ADDON_LOG_DEBUG, "Got Device Serial Number: %s, DeviceID: %s, Device Number: %s", m_device_serial.c_str(), m_device_id.c_str(), m_device_number.c_str());
  return true;
}

bool CPVREon::GetDeviceFromSerial()
{
  std::string jsonString;
  int statusCode = 0;

  std::string postData = "{\"deviceName\":\"\",\"deviceType\":\"" + DEVICE_TYPE +
                          "\",\"modelName\":\"" + DEVICE_MODEL +
                          "\",\"platform\":\"" + DEVICE_PLATFORM +
                          "\",\"serial\":\"" + m_device_serial +
                          "\",\"clientSwVersion\":\"\",\"systemSwVersion\":{\"name\":\"" + SYSTEM_SW +
                          "\",\"version\":\"" + SYSTEM_VERSION + "\"}}";

  jsonString = m_httpClient->HttpPost(m_api + "v1/devices", postData, statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get devices");
    return false;
  }

  m_device_id = std::to_string(Utils::JsonIntOrZero(doc, "deviceId"));
  m_device_number = Utils::JsonStringOrEmpty(doc, "deviceNumber");
  kodi::Log(ADDON_LOG_DEBUG, "Got Device ID: %s and Device Number: %s", m_device_id.c_str(), m_device_number.c_str());

  return true;
}

bool CPVREon::GetServers()
{
  std::string jsonString;
  int statusCode = 0;

  jsonString = m_httpClient->HttpGet(m_api + "v1/servers", statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get servers");
    return false;
  }

  const rapidjson::Value& liveServers = doc["live_servers"];
  for (rapidjson::Value::ConstValueIterator itr1 = liveServers.Begin();
      itr1 != liveServers.End(); ++itr1)
  {
    const rapidjson::Value& liveServer = (*itr1);
    EonServer eon_server;

    eon_server.id = Utils::JsonStringOrEmpty(liveServer, "id");
    eon_server.ip = Utils::JsonStringOrEmpty(liveServer, "ip");
    eon_server.hostname = Utils::JsonStringOrEmpty(liveServer, "hostname");

    kodi::Log(ADDON_LOG_DEBUG, "Got Live Server: %s %s %s", eon_server.id.c_str(), eon_server.ip.c_str(), eon_server.hostname.c_str());
    m_live_servers.emplace_back(eon_server);
  }

  const rapidjson::Value& timeshiftServers = doc["timeshift_servers"];
  for (rapidjson::Value::ConstValueIterator itr1 = timeshiftServers.Begin();
      itr1 != timeshiftServers.End(); ++itr1)
  {
    const rapidjson::Value& server = (*itr1);
    EonServer eon_server;

    eon_server.id = Utils::JsonStringOrEmpty(server, "id");
    eon_server.ip = Utils::JsonStringOrEmpty(server, "ip");
    eon_server.hostname = Utils::JsonStringOrEmpty(server, "hostname");

    kodi::Log(ADDON_LOG_DEBUG, "Got Timeshift Server: %s %s %s", eon_server.id.c_str(), eon_server.ip.c_str(), eon_server.hostname.c_str());
    m_timeshift_servers.emplace_back(eon_server);
  }

  return true;
}


bool CPVREon::GetHouseholds()
{
  std::string jsonString;
  int statusCode = 0;

  jsonString = m_httpClient->HttpGet(m_api + "v1/households", statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get households");
    return false;
  }

  m_subscriber_id = std::to_string(Utils::JsonIntOrZero(doc, "id"));
  kodi::Log(ADDON_LOG_DEBUG, "Got Subscriber ID: %s", m_subscriber_id.c_str());

  return true;
}

std::string CPVREon::GetTime()
{
  std::string jsonString;
  int statusCode = 0;

  jsonString = m_httpClient->HttpGet(m_api + "v1/time", statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get time");
    return "";
  }

  return Utils::JsonStringOrEmpty(doc, "time");
}

bool CPVREon::GetServiceProvider()
{
  std::string jsonString;
  int statusCode = 0;

  jsonString = m_httpClient->HttpGet(m_api + "v1/sp", statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get service provider");
    return false;
  }
  m_service_provider = Utils::JsonStringOrEmpty(doc, "identifier");
  m_support_web = Utils::JsonStringOrEmpty(doc, "supportWebAddress");
  kodi::Log(ADDON_LOG_DEBUG, "Got Service Provider: %s and Support Web: %s", m_service_provider.c_str(), m_support_web.c_str());

  return true;
}

bool CPVREon::GetRenderingProfiles()
{
  std::string jsonString;
  int statusCode = 0;

  jsonString = m_httpClient->HttpGet(m_api + "v1/rndprofiles", statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get rendering profiles");
    return false;
  }
  //kodi::Log(ADDON_LOG_DEBUG, "Got Rendering Profiles: %s", jsonString.c_str());

  const rapidjson::Value& rndProfiles = doc;

  for (rapidjson::Value::ConstValueIterator itr1 = rndProfiles.Begin();
      itr1 != rndProfiles.End(); ++itr1)
  {
    const rapidjson::Value& rndProfileItem = (*itr1);

    EonRenderingProfile eon_rndprofile;

    eon_rndprofile.id = Utils::JsonIntOrZero(rndProfileItem, "id");
    eon_rndprofile.name = Utils::JsonStringOrEmpty(rndProfileItem, "name");
    eon_rndprofile.coreStreamId = Utils::JsonStringOrEmpty(rndProfileItem, "coreStreamId");
    eon_rndprofile.height = Utils::JsonIntOrZero(rndProfileItem, "height");
    eon_rndprofile.width = Utils::JsonIntOrZero(rndProfileItem, "width");
    eon_rndprofile.audioBitrate = Utils::JsonIntOrZero(rndProfileItem, "audioBitrate");
    eon_rndprofile.videoBitrate = Utils::JsonIntOrZero(rndProfileItem, "videoBitrate");

    m_rendering_profiles.emplace_back(eon_rndprofile);
  }
  return true;
}

bool CPVREon::GetCategories(const bool isRadio)
{
  std::string jsonString;
  int statusCode = 0;

  if (isRadio) {
    jsonString = m_httpClient->HttpGet(m_api + "v2/categories/RADIO", statusCode);
  } else {
    jsonString = m_httpClient->HttpGet(m_api + "v2/categories/TV", statusCode);
  }

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get categories");
    return false;
  }
  //kodi::Log(ADDON_LOG_DEBUG, "Got C: %s", m_service_provider.c_str());
  const rapidjson::Value& categories = doc;

  for (rapidjson::Value::ConstValueIterator itr1 = categories.Begin();
      itr1 != categories.End(); ++itr1)
  {
    const rapidjson::Value& categoryItem = (*itr1);

    EonCategory eon_category;

    eon_category.id = Utils::JsonIntOrZero(categoryItem, "id");
    eon_category.order = Utils::JsonIntOrZero(categoryItem, "order");
    eon_category.name = Utils::JsonStringOrEmpty(categoryItem, "name");
    eon_category.isRadio = isRadio;
    eon_category.isDefault = Utils::JsonBoolOrFalse(categoryItem, "defaultList");

    const rapidjson::Value& channels = categoryItem["channels"];
    for (rapidjson::Value::ConstValueIterator itr2 = channels.Begin();
        itr2 != channels.End(); ++itr2)
    {
      const rapidjson::Value& channelItem = (*itr2);

      EonCategoryChannel eon_category_channel;

      eon_category_channel.id = Utils::JsonIntOrZero(channelItem, "id");
      eon_category_channel.position = Utils::JsonIntOrZero(channelItem, "position");

      eon_category.channels.emplace_back(eon_category_channel);
    }
    m_categories.emplace_back(eon_category);
  }
  return true;
}

CPVREon::CPVREon() :
  m_settings(new CSettings())
{
  m_settings->Load();
  m_httpClient = new HttpClient(m_settings);

  if (GetCDNInfo()) {
    std::string cdn_identifier = GetBrandIdentifier();
    kodi::Log(ADDON_LOG_DEBUG, "CDN Identifier: %s", cdn_identifier.c_str());
    std::string baseApi = GetBaseApi(cdn_identifier);
    m_api = "https://api-web." + baseApi + "/";
    m_images_api = "https://images-web." + baseApi + "/";
  } else {
    m_api = "https://api-web." + GLOBAL_URL;
    m_images_api = "https://images-web." + GLOBAL_URL;
  }
  m_httpClient->SetApi(m_api);
  kodi::Log(ADDON_LOG_DEBUG, "API set to: %s", m_api.c_str());

  m_device_id = m_settings->GetEonDeviceID();
  m_device_number = m_settings->GetEonDeviceNumber();

  m_ss_identity = m_settings->GetSSIdentity();
  if (m_ss_identity.empty()) {
    m_httpClient->RefreshSSToken();
    m_ss_identity = m_settings->GetSSIdentity();
  }

  if (m_device_id.empty() || m_device_number.empty()) {
    if (GetDeviceData()) {
      m_settings->SetSetting("deviceid", m_device_id);
      m_settings->SetSetting("devicenumber", m_device_number);
      m_settings->SetSetting("deviceserial", m_device_serial);
    }
    m_device_serial = m_settings->GetEonDeviceSerial();
    if (m_device_serial.empty()) {
      std::string m_device_serial = m_httpClient->GetUUID();
      m_settings->SetSetting("deviceserial", m_device_serial);
      kodi::Log(ADDON_LOG_DEBUG, "Generated Device Serial: %s", m_device_serial.c_str());
    }
    if (GetDeviceFromSerial()) {
      m_settings->SetSetting("deviceid", m_device_id);
      m_settings->SetSetting("devicenumber", m_device_number);
    }
  }

  m_subscriber_id = m_settings->GetEonSubscriberID();
  if (m_subscriber_id.empty()) {
    if (GetHouseholds()) {
      m_settings->SetSetting("subscriberid", m_subscriber_id);
    }
  }

  if (m_service_provider.empty() || m_support_web.empty()) {
    GetServiceProvider();
  }

  GetRenderingProfiles();
  GetServers();

//  }
  if (m_settings->IsTVenabled()) {
    GetCategories(false);
    LoadChannels(false);
  }
  if (m_settings->IsRadioenabled()) {
    GetCategories(true);
    LoadChannels(true);
  }

}

CPVREon::~CPVREon()
{
  m_channels.clear();
}

ADDON_STATUS CPVREon::SetSetting(const std::string& settingName, const std::string& settingValue)
{
  ADDON_STATUS result = m_settings->SetSetting(settingName, settingValue);
  if (!m_settings->VerifySettings()) {
    return ADDON_STATUS_NEED_SETTINGS;
  }
  return result;
}

bool CPVREon::LoadChannels(const bool isRadio)
{
  kodi::Log(ADDON_LOG_DEBUG, "Load Eon Channels");
  std::string jsonString;
  int statusCode = 0;
//  std::string postData = "{}";

  if (isRadio) {
    jsonString = m_httpClient->HttpGet(m_api + "v3/channels?channelType=RADIO&channelSort=RECOMMENDED&sortDir=DESC", statusCode);
  } else {
    jsonString = m_httpClient->HttpGet(m_api + "v3/channels?channelType=TV", statusCode);
  }

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to load channels");
    return false;
  }

  int currentnumber = 0;
  int startnumber = m_settings->GetStartNum()-1;
  int lastnumber = startnumber;
  const rapidjson::Value& channels = doc;

  for (rapidjson::Value::ConstValueIterator itr1 = channels.Begin();
      itr1 != channels.End(); ++itr1)
  {
    const rapidjson::Value& channelItem = (*itr1);

    std::string channame = Utils::JsonStringOrEmpty(channelItem, "name");

    EonChannel eon_channel;

    eon_channel.bRadio = isRadio;
    eon_channel.bArchive = Utils::JsonBoolOrFalse(channelItem, "cutvEnabled");
    eon_channel.strChannelName = channame;
    int ref_id = Utils::JsonIntOrZero(channelItem, "id");

    currentnumber = startnumber + GetDefaultNumber(isRadio, ref_id);
    if (currentnumber != 0) {
      eon_channel.iChannelNumber = currentnumber;
      lastnumber = currentnumber++;
    } else {
      eon_channel.iChannelNumber = ++lastnumber;
    }

    eon_channel.iUniqueId = ref_id;

    eon_channel.aaEnabled = Utils::JsonBoolOrFalse(channelItem, "aaEnabled");
    eon_channel.subscribed = Utils::JsonBoolOrFalse(channelItem, "subscribed");
    const rapidjson::Value& categories = channelItem["categories"];
    for (rapidjson::Value::ConstValueIterator itr2 = categories.Begin();
        itr2 != categories.End(); ++itr2)
    {
      const rapidjson::Value& categoryItem = (*itr2);

      EonChannelCategory cat;

      cat.id = Utils::JsonIntOrZero(categoryItem, "id");
      cat.primary = Utils::JsonBoolOrFalse(categoryItem, "primary");

      eon_channel.categories.emplace_back(cat);
    }
    const rapidjson::Value& images = channelItem["images"];
    for (rapidjson::Value::ConstValueIterator itr2 = images.Begin();
        itr2 != images.End(); ++itr2)
    {
      const rapidjson::Value& imageItem = (*itr2);

      if (Utils::JsonStringOrEmpty(imageItem, "size") == "XL") {
        eon_channel.strIconPath = m_images_api + Utils::JsonStringOrEmpty(imageItem, "path");
      }
    }
    const rapidjson::Value& pp = channelItem["publishingPoint"];
    for (rapidjson::Value::ConstValueIterator itr2 = pp.Begin();
        itr2 != pp.End(); ++itr2)
    {
      const rapidjson::Value& ppItem = (*itr2);

      EonPublishingPoint pp;

      pp.publishingPoint = Utils::JsonStringOrEmpty(ppItem, "publishingPoint");
      pp.audioLanguage =  Utils::JsonStringOrEmpty(ppItem, "audioLanguage");
      pp.subtitleLanguage =  Utils::JsonStringOrEmpty(ppItem, "subtitleLanguage");

      const rapidjson::Value& profileIds = ppItem["profileIds"];
      for (rapidjson::Value::ConstValueIterator itr3 = profileIds.Begin();
          itr3 != profileIds.End(); ++itr3)
      {
        pp.profileIds.emplace_back(itr3->GetInt());
      }

      const rapidjson::Value& playerCfgs = ppItem["playerCfgs"];
      for (rapidjson::Value::ConstValueIterator itr3 = playerCfgs.Begin();
          itr3 != playerCfgs.End(); ++itr3)
      {
        const rapidjson::Value& playerCfgItem = (*itr3);
        if (Utils::JsonStringOrEmpty(playerCfgItem, "type") ==  "live") {
          eon_channel.sig = Utils::JsonStringOrEmpty(playerCfgItem, "sig");
        }
      }
      eon_channel.publishingPoints.emplace_back(pp);
    }

    if (!m_settings->HideUnsubscribed() || eon_channel.subscribed) {
      kodi::Log(ADDON_LOG_DEBUG, "%i. Channel Name: %s ID: %i", lastnumber, channame.c_str(), ref_id);
      m_channels.emplace_back(eon_channel);
    }
  }

  return true;
}

bool CPVREon::HandleSession(bool start, int cid, int epg_id)
{
  std::string time = GetTime();
  std::string epoch = time.substr(0,10);
  std::string ms = time.substr(10,3);
  time_t timestamp = atoll(epoch.c_str());
  std::string datetime = Utils::TimeToString(timestamp) + "." + ms + "Z";
  std::string offset = Utils::TimeToString(timestamp-300) + "." + ms + "Z";
  kodi::Log(ADDON_LOG_DEBUG, "Handle Session time: %s", datetime.c_str());
  std::string action;
  if (start) {
    action = "start";
  }
  else {
    action = "stop";
  }

  std::string postData = "[{\"time\":\"" + datetime +
                         "\",\"type\":\"CUTV\",\"rnd_profile\":\"hp7000\",\"session_id\":\"" + m_session_id +
                         "\",\"action\":\"" + action +
                         "\",\"device\":{\"id\":" + m_device_id +
                         "},\"subscriber\":{\"id\":" + m_subscriber_id +
                         "},\"offset_time\":\"" + offset +
                         "\",\"channel\":{\"id\":" + std::to_string(cid) +
                         "},\"epg_event_id\":" + std::to_string(epg_id) +
                         ",\"viewing_time\":500,\"silent_event_change\":false}]";
  kodi::Log(ADDON_LOG_DEBUG, "Session PostData: %s", postData.c_str());

  int statusCode = 0;
  std::string jsonString = m_httpClient->HttpPost("https://aes.ug.cdn.united.cloud/v1/events", postData, statusCode);

  kodi::Log(ADDON_LOG_DEBUG, "Event register returned: %s", jsonString.c_str());

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to register event");
    return false;
  }

  return Utils::JsonBoolOrFalse(doc, "success");
}

void CPVREon::SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                                    const std::string& url,
                                    bool realtime, bool playTimeshiftBuffer,
                                    const std::string& license)
{
  kodi::Log(ADDON_LOG_DEBUG, "[PLAY STREAM] url: %s", url.c_str());

  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, url);
  properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
  properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, realtime ? "true" : "false");

  kodi::Log(ADDON_LOG_DEBUG, "[PLAY STREAM] hls");
  properties.emplace_back("inputstream.adaptive.manifest_type", "hls");
  properties.emplace_back(PVR_STREAM_PROPERTY_MIMETYPE, "application/x-mpegURL");
  properties.emplace_back("inputstream.adaptive.original_audio_language", "bs");
  properties.emplace_back("inputstream.adaptive.stream_selection_type", "adaptive");

//  properties.emplace_back("inputstream.adaptive.license_type", "com.widevine.alpha");
//  properties.emplace_back("inputstream.adaptive.license_key",
//                          "https://lic.drmtoday.com/license-proxy-widevine/cenc/"
//                          "|Content-Type=text%2Fxml&dt-custom-data=" +
//                              license + "|R{SSM}|JBlicense");

  properties.emplace_back("inputstream.adaptive.manifest_update_parameter", "full");
}


PVR_ERROR CPVREon::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsTV(m_settings->IsTVenabled());
  capabilities.SetSupportsRadio(m_settings->IsRadioenabled());
  capabilities.SetSupportsChannelGroups(m_settings->IsGroupsenabled());
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

PVR_ERROR CPVREon::GetBackendName(std::string& name)
{
  name = "eon pvr add-on";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetBackendVersion(std::string& version)
{
  version = "0.1";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetConnectionString(std::string& connection)
{
  connection = "connected";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetBackendHostname(std::string& hostname)
{
  hostname = "";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  total = 1024 * 1024 * 1024;
  used = 0;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetEPGForChannel(int channelUid,
                                     time_t start,
                                     time_t end,
                                     kodi::addon::PVREPGTagsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  for (const auto& channel : m_channels)
  {

    if (channel.iUniqueId != channelUid)
      continue;


    std::string jsonEpg;
    int statusCode = 0;

    kodi::Log(ADDON_LOG_DEBUG, "EPG Request for Channel %u Start %u End %u", channel.iUniqueId, start, end);

    std::string url = m_api + "v1/events/epg";
    std::string params = "?cid=" + std::to_string(channel.iUniqueId) +
                         "&fromTime=" + std::to_string(start*1000) +
                         "&toTime=" + std::to_string(end*1000);

    jsonEpg = m_httpClient->HttpGet(url + params, statusCode);

//    kodi::Log(ADDON_LOG_DEBUG, "EPG Events returned: %s", jsonEpg.c_str());

    rapidjson::Document epgDoc;
    epgDoc.Parse(jsonEpg.c_str());
    if (epgDoc.GetParseError())
    {
      kodi::Log(ADDON_LOG_ERROR, "[GetEPG] ERROR: error while parsing json");
      return PVR_ERROR_SERVER_ERROR;
    }
    kodi::Log(ADDON_LOG_DEBUG, "[epg] iterate entries");

//    std::string cid = "\"" + std::to_string(channel.referenceID) + "\"";
    std::string cid = std::to_string(channel.iUniqueId);
//    kodi::Log(ADDON_LOG_DEBUG, "EPG Channel ReferenceID: %s", cid.c_str());
    const rapidjson::Value& epgitems = epgDoc[cid.c_str()];
//    kodi::Log(ADDON_LOG_DEBUG, "EPG Items: %s", epgitems.c_str());
    for (rapidjson::Value::ConstValueIterator itr1 = epgitems.Begin();
        itr1 != epgitems.End(); ++itr1)
    {
      const rapidjson::Value& epgItem = (*itr1);

      kodi::addon::PVREPGTag tag;

      tag.SetUniqueBroadcastId(Utils::JsonIntOrZero(epgItem,"id"));
      tag.SetUniqueChannelId(channelUid);
      tag.SetTitle(Utils::JsonStringOrEmpty(epgItem,"title"));
      int64_t starttime = Utils::JsonInt64OrZero(epgItem,"startTime") / 1000;
      int64_t endtime = Utils::JsonInt64OrZero(epgItem,"endTime") / 1000;
      tag.SetStartTime((int)starttime);
      tag.SetEndTime((int)endtime);
      tag.SetPlot(Utils::JsonStringOrEmpty(epgItem,"shortDescription"));

      const rapidjson::Value& images = epgItem["images"];
      for (rapidjson::Value::ConstValueIterator itr2 = images.Begin();
          itr2 != images.End(); ++itr2)
      {
        const rapidjson::Value& imageItem = (*itr2);

        if (Utils::JsonStringOrEmpty(imageItem, "size") == "STB_XL") {
          tag.SetIconPath(m_images_api + Utils::JsonStringOrEmpty(imageItem, "path"));
        }
      }
      kodi::Log(ADDON_LOG_DEBUG, "%u adding EPG: ID: %u Title: %s Start: %u End: %u", channelUid, Utils::JsonIntOrZero(epgItem,"id"), Utils::JsonStringOrEmpty(epgItem,"title").c_str(),Utils::JsonIntOrZero(epgItem,"startTime")/1000,Utils::JsonIntOrZero(epgItem,"endTime")/1000);
      results.Add(tag);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  bIsPlayable = false;

  for (const auto& channel : m_channels)
  {
    if (channel.iUniqueId == tag.GetUniqueChannelId())
    {
      auto current_time = time(NULL);
      if (current_time > tag.GetStartTime())
      {
        bIsPlayable = channel.bArchive;
      }
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetEPGTagStreamProperties(
    const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  for (const auto& channel : m_channels)
  {
    if (channel.iUniqueId == tag.GetUniqueChannelId())
    {
      return GetStreamProperties(channel, properties, tag.GetStartTime(), false);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetProvidersAmount(int& amount)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetProviders(kodi::addon::PVRProvidersResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetChannelsAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  amount = m_channels.size();
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Channels Amount: [%s]", amount_str.c_str());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetChannels(bool bRadio, kodi::addon::PVRChannelsResultSet& results)
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

//       PVR API 8.0.0
//      kodiChannel.SetClientProviderUid(channel.iProviderId);

      results.Add(kodiChannel);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetStreamProperties(
    const EonChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties, int starttime, bool isLive)
{
    std::string streaming_profile = "hp7000";

    unsigned int rndbitrate = 0;
    unsigned int current_bitrate = 0;
    unsigned int current_id = 0;
    for (unsigned int i = 0; i < channel.publishingPoints[0].profileIds.size(); i++) {
        current_bitrate = getBitrate(channel.bRadio, channel.publishingPoints[0].profileIds[i]);
        if (current_bitrate > rndbitrate) {
          current_id = channel.publishingPoints[0].profileIds[i];
          rndbitrate = current_bitrate;
        }
    }
    if (current_id == 0) {
      return PVR_ERROR_NO_ERROR;
    } else {
      streaming_profile = getCoreStreamId(current_id);
    }
    kodi::Log(ADDON_LOG_DEBUG, "Channel Rendering Profile -> %u", current_id);

    m_session_id = Utils::CreateUUID();

    EonServer currentServer;

    GetServer(isLive, currentServer);

    std::string plain_aes = "channel=" + channel.publishingPoints[0].publishingPoint + ";" +
                            "stream=" + streaming_profile + ";" + "sp=" + m_service_provider + ";" +
                            "u=" + m_settings->GetEonStreamUser() + ";" +
                            "ss=" + m_settings->GetEonStreamKey() + ";" +
                            "minvbr=100;adaptive=true;player=" + PLAYER + ";" +
                            "sig=" + channel.sig + ";" +
                            "session=" + m_session_id + ";" +
                            "m=" + currentServer.ip + ";" +
                            "device=" + m_settings->GetEonDeviceNumber() + ";" +
                            "ctime=" + GetTime() + ";";
    if (!isLive) {
      plain_aes = plain_aes + "t=" + std::to_string(starttime) + "000;";
    }
    plain_aes = plain_aes + "conn=BROWSER;aa=" + (channel.aaEnabled ? "true" : "false");
    kodi::Log(ADDON_LOG_DEBUG, "Plain AES -> %s", plain_aes.c_str());

    Botan::AutoSeeded_RNG rng;

    Botan::secure_vector<uint8_t> key = Botan::base64_decode(urlsafedecode(m_settings->GetEonStreamKey()));

    auto enc = Botan::Cipher_Mode::create("AES-128/CBC/PKCS7", Botan::Cipher_Dir::ENCRYPTION);
    enc->set_key(key);

    // generate fresh nonce (IV)
    Botan::secure_vector<uint8_t> iv = rng.random_vec(enc->default_nonce_length());

    // Copy input data to a buffer that will be encrypted
    Botan::secure_vector<uint8_t> pt(plain_aes.data(), plain_aes.data() + plain_aes.length());

    enc->start(iv);
    enc->finish(pt);

    kodi::Log(ADDON_LOG_DEBUG, "IV -> %s", Botan::hex_encode(iv).c_str());
    kodi::Log(ADDON_LOG_DEBUG, "IV (base64) -> %s", urlsafeencode(Botan::base64_encode(iv)).c_str());
    kodi::Log(ADDON_LOG_DEBUG, "Key -> %s", Botan::hex_encode(key).c_str());
    kodi::Log(ADDON_LOG_DEBUG, "Encrypted -> %s", Botan::hex_encode(pt).c_str());
    kodi::Log(ADDON_LOG_DEBUG, "Encrypted (base64) -> %s", urlsafeencode(Botan::base64_encode(pt)).c_str());

    std::string enc_url = "https://" + currentServer.hostname +
                          "/stream?i=" + urlsafeencode(Botan::base64_encode(iv)) +
                          "&a=" + urlsafeencode(Botan::base64_encode(pt)) +
                          "&sp=" + m_service_provider +
                          "&u=" + m_settings->GetEonStreamUser() +
                          "&player=" + PLAYER +
                          "&session=" + m_session_id +
                          "&sig=" + channel.sig;

    kodi::Log(ADDON_LOG_DEBUG, "Encrypted Stream URL -> %s", enc_url.c_str());

    SetStreamProperties(properties, enc_url, true, false, "");

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetChannelStreamProperties(
    const kodi::addon::PVRChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  EonChannel addonChannel;
  GetChannel(channel, addonChannel);

  return GetStreamProperties(addonChannel, properties, 0, true);
}

PVR_ERROR CPVREon::GetChannelGroupsAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  amount = static_cast<int>(m_categories.size());
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Groups Amount: [%s]", amount_str.c_str());

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetChannelGroups(bool bRadio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::vector<EonCategory>::iterator it;
  for (it = m_categories.begin(); it != m_categories.end(); ++it)
  {
    kodi::addon::PVRChannelGroup kodiGroup;

    if (bRadio == it->isRadio) {
      kodiGroup.SetPosition(it->order);
      kodiGroup.SetIsRadio(it->isRadio); /* is radio group */
      kodiGroup.SetGroupName(it->name);

      results.Add(kodiGroup);
      kodi::Log(ADDON_LOG_DEBUG, "Group added: %s at postion %u", it->name.c_str(), it->order);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                           kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  for (const auto& cgroup : m_categories)
  {
    if (cgroup.name != group.GetGroupName())
      continue;

    for (const auto& channel : cgroup.channels)
    {
      kodi::addon::PVRChannelGroupMember kodiGroupMember;

      kodiGroupMember.SetGroupName(group.GetGroupName());
      kodiGroupMember.SetChannelUniqueId(static_cast<unsigned int>(channel.id));
      kodiGroupMember.SetChannelNumber(static_cast<unsigned int>(channel.position));

      results.Add(kodiGroupMember);
    }
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus)
{
  signalStatus.SetAdapterName("pvr eon backend");
  signalStatus.SetAdapterStatus("OK");

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetRecordingsAmount(bool deleted, int& amount)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetRecordingStreamProperties(
    const kodi::addon::PVRRecording& recording,
    std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  /* TODO: Implement this to get support for the timer features introduced with PVR API 1.9.7 */
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR CPVREon::GetTimersAmount(int& amount)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::CallEPGMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                    const kodi::addon::PVREPGTag& item)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);


  return CallMenuHook(menuhook);
}

PVR_ERROR CPVREon::CallChannelMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                        const kodi::addon::PVRChannel& item)
{
  return CallMenuHook(menuhook);
}

PVR_ERROR CPVREon::CallTimerMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                      const kodi::addon::PVRTimer& item)
{
  return CallMenuHook(menuhook);
}

PVR_ERROR CPVREon::CallRecordingMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                          const kodi::addon::PVRRecording& item)
{
  return CallMenuHook(menuhook);
}

PVR_ERROR CPVREon::CallSettingsMenuHook(const kodi::addon::PVRMenuhook& menuhook)
{
  return CallMenuHook(menuhook);
}

PVR_ERROR CPVREon::CallMenuHook(const kodi::addon::PVRMenuhook& menuhook)
{
/*
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
*/
  return PVR_ERROR_NO_ERROR;
}

bool CPVREon::GetChannel(const kodi::addon::PVRChannel& channel, EonChannel& myChannel)
{
  for (const auto& thisChannel : m_channels)
  {

    if (thisChannel.iUniqueId == (int)channel.GetUniqueId())
    {
      myChannel.iUniqueId = thisChannel.iUniqueId;
      myChannel.bRadio = thisChannel.bRadio;
      myChannel.bArchive = thisChannel.bArchive;
      myChannel.iChannelNumber = thisChannel.iChannelNumber;
//      myChannel.iSubChannelNumber = thisChannel.iSubChannelNumber;
//      myChannel.iEncryptionSystem = thisChannel.iEncryptionSystem;
//      myChannel.referenceID = thisChannel.referenceID;
      myChannel.strChannelName = thisChannel.strChannelName;
      myChannel.strIconPath = thisChannel.strIconPath;
      myChannel.publishingPoints = thisChannel.publishingPoints;
      myChannel.categories = thisChannel.categories;
//      mychannel.subtitleLanguage = thisChannel.subtitleLanguage;
      myChannel.sig = thisChannel.sig;
      myChannel.aaEnabled = thisChannel.aaEnabled;
      myChannel.subscribed = thisChannel.subscribed;
//      myChannel.profileIds = thisChannel.profileIds;
//      myChannel.strStreamURL = thisChannel.strStreamURL;

      return true;
    }
  }

  return false;
}

bool CPVREon::GetServer(bool isLive, EonServer& myServer)
{
  std::vector<EonServer> servers;
  if (isLive) {
    servers = m_live_servers;
  } else {
    servers = m_timeshift_servers;
  }
  int count = 0;
  for (const auto& thisServer : servers)
  {
      count++;
      if (count == 2) {
        myServer.id = thisServer.id;
        myServer.ip = thisServer.ip;
        myServer.hostname = thisServer.hostname;
        return true;
      }
  }
  return false;
}

std::string CPVREon::GetRecordingURL(const kodi::addon::PVRRecording& recording)
{
  return "";
}

ADDONCREATOR(CPVREon)
