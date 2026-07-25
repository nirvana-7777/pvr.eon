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
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include <kodi/Filesystem.h>
#include <kodi/General.h>
#include <kodi/gui/dialogs/OK.h>
#include "Utils.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "Base64.h"

#define CBC 1

#include "aes.hpp"
#include "pkcs7_padding.hpp"

static const uint8_t block_size = 16;

namespace
{
constexpr int64_t PVR_TIME_BASE = 1000000;
constexpr time_t PENDING_PLAYBACK_TTL_SECONDS = 15;
constexpr int64_t NATIVE_VIRTUAL_UNITS_PER_SECOND = 1000;
constexpr time_t NATIVE_SEEK_RESTART_EPSILON_SECONDS = 2;
constexpr time_t NATIVE_LIVE_EDGE_DELAY_SECONDS = 15;
constexpr int64_t NATIVE_INITIAL_SEEK_IGNORE_WINDOW_MS = 4000;
constexpr int NATIVE_POLL_RETRY_COUNT = 10;
constexpr auto NATIVE_POLL_RETRY_DELAY = std::chrono::milliseconds(200);

int64_t MonotonicNowMs()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

const char* BoolState(bool value)
{
  return value ? "true" : "false";
}

const char* PlatformName(int platform)
{
  switch (platform)
  {
    case PLATFORM_WEB:
      return "web";
    case PLATFORM_ANDROIDTV:
      return "androidtv";
    default:
      return "unknown";
  }
}

const char* InputstreamName(int inputstream)
{
  switch (inputstream)
  {
    case INPUTSTREAM_ADAPTIVE:
      return "inputstream.adaptive";
    case INPUTSTREAM_FFMPEGDIRECT:
      return "inputstream.ffmpegdirect";
    default:
      return "unknown";
  }
}

std::string DescribeValue(const std::string& value)
{
  return value.empty() ? "empty" : "set(len=" + std::to_string(value.size()) + ")";
}

std::string PreviewForLog(std::string value)
{
  std::replace(value.begin(), value.end(), '\n', ' ');
  std::replace(value.begin(), value.end(), '\r', ' ');
  if (value.size() > 200)
    value = value.substr(0, 200) + "...";
  return value;
}

size_t CountOccurrences(const std::string& haystack, const std::string& needle)
{
  if (needle.empty())
    return 0;

  size_t count = 0;
  size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos)
  {
    ++count;
    pos += needle.size();
  }
  return count;
}

std::string Trim(std::string value)
{
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t'))
    value.pop_back();

  size_t start = 0;
  while (start < value.size() && (value[start] == ' ' || value[start] == '\t'))
    ++start;

  return value.substr(start);
}

std::string ResolvePlaylistUrl(const std::string& baseUrl, const std::string& childUrl)
{
  if (childUrl.empty())
    return "";

  if (childUrl.rfind("https://", 0) == 0 || childUrl.rfind("http://", 0) == 0)
    return childUrl;

  const size_t schemePos = baseUrl.find("://");
  if (schemePos == std::string::npos)
    return childUrl;

  const std::string scheme = baseUrl.substr(0, schemePos);
  const size_t authorityStart = schemePos + 3;
  const size_t pathStart = baseUrl.find('/', authorityStart);
  const std::string authority = pathStart == std::string::npos
                                    ? baseUrl.substr(authorityStart)
                                    : baseUrl.substr(authorityStart, pathStart - authorityStart);

  if (childUrl.rfind("//", 0) == 0)
    return scheme + ":" + childUrl;

  if (childUrl.front() == '/')
    return scheme + "://" + authority + childUrl;

  const size_t lastSlash = baseUrl.rfind('/');
  if (lastSlash == std::string::npos || lastSlash < authorityStart)
    return scheme + "://" + authority + "/" + childUrl;

  return baseUrl.substr(0, lastSlash + 1) + childUrl;
}

std::string FirstVariantPlaylistUrl(const std::string& manifestBody, const std::string& baseUrl)
{
  size_t pos = 0;
  while (pos < manifestBody.size())
  {
    const size_t lineEnd = manifestBody.find('\n', pos);
    const std::string line = Trim(manifestBody.substr(pos, lineEnd == std::string::npos ? std::string::npos : lineEnd - pos));
    pos = lineEnd == std::string::npos ? manifestBody.size() : lineEnd + 1;

    if (line.rfind("#EXT-X-STREAM-INF", 0) != 0)
      continue;

    while (pos < manifestBody.size())
    {
      const size_t uriEnd = manifestBody.find('\n', pos);
      const std::string uri = Trim(manifestBody.substr(pos, uriEnd == std::string::npos ? std::string::npos : uriEnd - pos));
      pos = uriEnd == std::string::npos ? manifestBody.size() : uriEnd + 1;
      if (uri.empty() || uri[0] == '#')
        continue;

      return ResolvePlaylistUrl(baseUrl, uri);
    }
  }

  return "";
}

std::vector<std::string> ExtractMediaSegmentUrls(const std::string& playlistBody,
                                                 const std::string& baseUrl)
{
  std::vector<std::string> urls;

  size_t pos = 0;
  while (pos < playlistBody.size())
  {
    const size_t lineEnd = playlistBody.find('\n', pos);
    const std::string line =
        Trim(playlistBody.substr(pos, lineEnd == std::string::npos ? std::string::npos : lineEnd - pos));
    pos = lineEnd == std::string::npos ? playlistBody.size() : lineEnd + 1;

    if (line.empty() || line[0] == '#')
      continue;

    urls.emplace_back(ResolvePlaylistUrl(baseUrl, line));
  }

  return urls;
}

std::string ExpectedBrandIdentifier(int providerSetting)
{
  switch (providerSetting)
  {
    case 0: // SBB
      return "sbb-qa";
    case 1: // Telemach
      return "telemach";
    case 3: // Vivacom
      return "vivacom";
    case 5: // Nova
      return "nova";
    default:
      return "";
  }
}

} // namespace

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

std::string string_to_hex(const std::string& input)
{
    static const char hex_digits[] = "0123456789ABCDEF";

    std::string output;
    output.reserve(input.length() * 2);
    for (unsigned char c : input)
    {
        output.push_back(hex_digits[c >> 4]);
        output.push_back(hex_digits[c & 15]);
    }
    return output;
}

std::string aes_encrypt_cbc(const std::string &iv_str, const std::string &key, const std::string &plaintext)
{
    uint8_t i;

    int dlen = strlen(plaintext.c_str());
    int klen = strlen(key.c_str());

    //Proper Length of plaintext
    int dlenu = dlen;

    dlenu += block_size - (dlen % block_size);
    kodi::Log(ADDON_LOG_DEBUG, "The original length of the plaintext is = %d and the length of the padded plaintext is = %d", dlen, dlenu);

    // Make the uint8_t arrays
    uint8_t hexarray[dlenu];
    uint8_t kexarray[klen];
    uint8_t iv[klen];

    // Initialize them with zeros
    memset( hexarray, 0, dlenu );
    memset( kexarray, 0, klen );
    memset( iv, 0, klen );

    // Fill the uint8_t arrays
    for (int i=0;i<dlen;i++) {
        hexarray[i] = (uint8_t)plaintext[i];
    }
    for (int i=0;i<klen;i++) {
        kexarray[i] = (uint8_t)key[i];
        iv[i] = (uint8_t)iv_str[i];
    }

    pkcs7_padding_pad_buffer( hexarray, dlen, sizeof(hexarray), block_size );

    //start the encryption
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, kexarray, iv);

    // encrypt
    AES_CBC_encrypt_buffer(&ctx, hexarray, dlenu);

    return std::string(reinterpret_cast<const char*>(hexarray), dlenu);
}

bool CPVREon::GetPostJson(const std::string& url, const std::string& body, rapidjson::Document& doc)
{
  int statusCode = 0;
  std::string result;

  if (body.empty()) {
    result = m_httpClient->HttpGet(url, statusCode);
  } else
  {
//    kodi::Log(ADDON_LOG_DEBUG, "Body: %s", body.c_str());
    result = m_httpClient->HttpPost(url, body, statusCode);
  }
  //kodi::Log(ADDON_LOG_DEBUG, "Result: %s", result.c_str());
  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206))
  {
    kodi::Log(ADDON_LOG_ERROR,
              "Failed to get JSON for URL %s. requestBodyLen=%zu responseLen=%zu parseError=%u status=%i",
              url.c_str(), body.size(), result.size(), doc.GetParseError(), statusCode);
    if (!result.empty())
      kodi::Log(ADDON_LOG_DEBUG, "JSON failure response preview: %s", PreviewForLog(result).c_str());
    if (!doc.GetParseError())
    {
      if (doc.HasMember("error") && doc.HasMember("errorMessage"))
      {
        std::string title = Utils::JsonStringOrEmpty(doc, "error");
        std::string abstract = Utils::JsonStringOrEmpty(doc, "errorMessage");
        kodi::gui::dialogs::OK::ShowAndGetInput(title, abstract);
      }
    }
    return false;
  }
  return true;
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
  std::string url = BROKER_URL + "v2/brands";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get brands");
    return "";
  }

  int i = 0;
  int sp_id = m_settings->GetEonServiceProvider();
  const std::string expected_identifier = ExpectedBrandIdentifier(sp_id);
  kodi::Log(ADDON_LOG_DEBUG, "Requested Service Provider ID:%u", sp_id);

  const rapidjson::Value& brands = doc;

  for (rapidjson::Value::ConstValueIterator itr1 = brands.Begin();
      itr1 != brands.End(); ++itr1)
  {
    const rapidjson::Value& brandItem = (*itr1);
    const std::string identifier = Utils::JsonStringOrEmpty(brandItem, "identifier");
    if (!expected_identifier.empty() && identifier == expected_identifier)
    {
      kodi::Log(ADDON_LOG_INFO,
                "Resolved provider setting %i via stable brand identifier '%s'.",
                sp_id, identifier.c_str());
      return Utils::JsonStringOrEmpty(brandItem, "cdnIdentifier");
    }

    if (expected_identifier.empty() && i == sp_id)
      return Utils::JsonStringOrEmpty(brandItem, "cdnIdentifier");

    i++;
  }

  if (!expected_identifier.empty())
  {
    kodi::Log(ADDON_LOG_WARNING,
              "Stable brand identifier '%s' was not found for provider setting %i. Falling back to legacy index mapping.",
              expected_identifier.c_str(), sp_id);

    i = 0;
    for (rapidjson::Value::ConstValueIterator itr1 = brands.Begin();
         itr1 != brands.End(); ++itr1)
    {
      if (i == sp_id)
      {
        const rapidjson::Value& brandItem = (*itr1);
        return Utils::JsonStringOrEmpty(brandItem, "cdnIdentifier");
      }
      i++;
    }
  }

  return "";
}

bool CPVREon::GetCDNInfo()
{
  std::string url = BROKER_URL + "v1/cdninfo";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
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

    cdn.baseApi = Utils::JsonStringOrEmpty(baseApi, EonParameters[m_platform].api_selector.c_str());
    m_cdns.emplace_back(cdn);
  }

  return true;
}

bool CPVREon::GetDeviceData()
{
  std::string url = m_support_web + "/gateway/SelfCareAPI/1.0/selfcareapi/" + SS_DOMAIN + "/subscriber/" + m_settings->GetSSIdentity() + "/devices/eon/2/product";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
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
  std::string postData;

  postData = "{\"deviceName\":\"" + EonParameters[m_platform].device_name +
                "\",\"deviceType\":\"" + EonParameters[m_platform].device_type +
                "\",\"modelName\":\"" + EonParameters[m_platform].device_model +
                "\",\"platform\":\"" + EonParameters[m_platform].device_platform +
                "\",\"serial\":\"" + m_device_serial +
                "\",\"clientSwVersion\":\"" + EonParameters[m_platform].client_sw_version;
  if (m_platform == PLATFORM_ANDROIDTV)
    postData += "\",\"clientSwBuild\":\"" + EonParameters[m_platform].client_sw_build;
  postData += "\",\"systemSwVersion\":{\"name\":\"" + EonParameters[m_platform].system_sw +
              "\",\"version\":\"" + EonParameters[m_platform].system_version + "\"}";
  if (m_platform == PLATFORM_ANDROIDTV)
    postData += ",\"fcmToken\":\"\""; //TODO: implement parameter fcmToken...
  postData += "}";

  std::string url = m_api + "v1/devices";

  rapidjson::Document doc;
  if (!GetPostJson(url, postData, doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get devices");
    return false;
  }

  m_device_id = std::to_string(Utils::JsonIntOrZero(doc, "deviceId"));
  m_device_number = Utils::JsonStringOrEmpty(doc, "deviceNumber");
  kodi::Log(ADDON_LOG_DEBUG, "Got Device ID: %s and Device Number: %s", m_device_id.c_str(), m_device_number.c_str());

  return true;
}

bool CPVREon::RefreshDeviceRegistration()
{
  kodi::Log(ADDON_LOG_INFO, "Refreshing stored device registration.");

  m_settings->SetSetting("accesstoken", "");
  m_settings->SetSetting("refreshtoken", "");
  m_settings->SetSetting("subscriberid", "");
  m_settings->SetSetting("streamkey", "");
  m_settings->SetSetting("streamuser", "");
  m_settings->SetSetting("deviceid", "");
  m_settings->SetSetting("devicenumber", "");

  m_device_id.clear();
  m_device_number.clear();
  m_subscriber_id.clear();

  m_device_serial = m_settings->GetEonDeviceSerial();
  if (m_device_serial.empty())
  {
    m_device_serial = m_httpClient->GetUUID();
    m_settings->SetSetting("deviceserial", m_device_serial);
    kodi::Log(ADDON_LOG_DEBUG, "Generated replacement device serial: %s", m_device_serial.c_str());
  }

  if (!GetDeviceFromSerial())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to refresh stored device registration.");
    return false;
  }

  m_settings->SetSetting("deviceid", m_device_id);
  m_settings->SetSetting("devicenumber", m_device_number);
  kodi::Log(ADDON_LOG_INFO, "Device registration refreshed. deviceId=%s deviceNumber=%s",
            DescribeValue(m_device_id).c_str(),
            DescribeValue(m_device_number).c_str());
  return true;
}

bool CPVREon::GetServers()
{
  std::string url = m_api + "v1/servers";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
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
  std::string url = m_api + "v1/households";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get households");
    return false;
  }

  m_subscriber_id = std::to_string(Utils::JsonIntOrZero(doc, "id"));
  kodi::Log(ADDON_LOG_DEBUG, "Got Subscriber ID: %s", m_subscriber_id.c_str());

  return true;
}

std::string CPVREon::GetTime()
{
  std::string url = m_api + "v1/time";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get time");
    return "";
  }

  return Utils::JsonStringOrEmpty(doc, "time");
}

bool CPVREon::GetServiceProvider()
{
  std::string url = m_api + "v1/sp";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get service provider");
    return false;
  }

  m_service_provider = Utils::JsonStringOrEmpty(doc, "identifier");
  m_support_web = Utils::JsonStringOrEmpty(doc, "supportWebAddress");
  m_httpClient->SetSupportApi(m_support_web);
  kodi::Log(ADDON_LOG_DEBUG, "Got Service Provider: %s and Support Web: %s", m_service_provider.c_str(), m_support_web.c_str());

  return true;
}

bool CPVREon::GetRenderingProfiles()
{
  std::string url = m_api + "v1/rndprofiles";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get rendering profiles");
    return false;
  }

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
  std::string url = m_api + "v2/categories/" + (isRadio ? "RADIO" : "TV");

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get categories");
    return false;
  }

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
  const bool settings_loaded = m_settings->Load();
  m_httpClient = new HttpClient(m_settings);

  m_platform = m_settings->GetPlatform();
  m_support_web = "API_NOT_SET_YET";
  m_httpClient->SetSupportApi(m_support_web);

  srand(time(nullptr));

  kodi::Log(ADDON_LOG_INFO,
            "Starting pvr.eon. platform=%s provider=%i tv=%s radio=%s groups=%s hideUnsubscribed=%s shortNames=%s ageRating=%i inputstream=%i settingsLoaded=%s settingsValid=%s",
            PlatformName(m_platform),
            m_settings->GetEonServiceProvider(),
            BoolState(m_settings->IsTVenabled()),
            BoolState(m_settings->IsRadioenabled()),
            BoolState(m_settings->IsGroupsenabled()),
            BoolState(m_settings->HideUnsubscribed()),
            BoolState(m_settings->UseShortNames()),
            m_settings->GetAgeRating(),
            m_settings->GetInputstream(),
            BoolState(settings_loaded),
            BoolState(m_settings->VerifySettings()));
  kodi::Log(ADDON_LOG_DEBUG,
            "Startup settings state. username=%s password=%s access=%s refresh=%s generic=%s deviceId=%s deviceNumber=%s deviceSerial=%s subscriberId=%s",
            DescribeValue(m_settings->GetEonUsername()).c_str(),
            DescribeValue(m_settings->GetEonPassword()).c_str(),
            DescribeValue(m_settings->GetEonAccessToken()).c_str(),
            DescribeValue(m_settings->GetEonRefreshToken()).c_str(),
            DescribeValue(m_settings->GetGenericAccessToken()).c_str(),
            DescribeValue(m_settings->GetEonDeviceID()).c_str(),
            DescribeValue(m_settings->GetEonDeviceNumber()).c_str(),
            DescribeValue(m_settings->GetEonDeviceSerial()).c_str(),
            DescribeValue(m_settings->GetEonSubscriberID()).c_str());

  if (GetCDNInfo()) {
    std::string cdn_identifier = GetBrandIdentifier();
    kodi::Log(ADDON_LOG_DEBUG, "CDN Identifier: %s", cdn_identifier.c_str());
    std::string baseApi = GetBaseApi(cdn_identifier);
    m_api = "https://api-" + EonParameters[m_platform].api_prefix + "." + baseApi + "/";
    m_images_api = "https://images-" + EonParameters[m_platform].api_prefix + "." + baseApi + "/";
  } else {
    m_api = "https://api-" + EonParameters[m_platform].api_prefix + "." + GLOBAL_URL;
    m_images_api = "https://images-" + EonParameters[m_platform].api_prefix + "." + GLOBAL_URL;
  }
  m_httpClient->SetApi(m_api);
  kodi::Log(ADDON_LOG_DEBUG, "API set to: %s", m_api.c_str());

  m_device_id = m_settings->GetEonDeviceID();
  m_device_number = m_settings->GetEonDeviceNumber();
  m_device_serial = m_settings->GetEonDeviceSerial();
  kodi::Log(ADDON_LOG_DEBUG, "Stored device state before init. deviceId=%s deviceNumber=%s deviceSerial=%s",
            DescribeValue(m_device_id).c_str(),
            DescribeValue(m_device_number).c_str(),
            DescribeValue(m_device_serial).c_str());
/*
  m_ss_identity = m_settings->GetSSIdentity();
  if (m_ss_identity.empty()) {
    m_httpClient->RefreshSSToken();
    m_ss_identity = m_settings->GetSSIdentity();
  }
*/
  if (m_device_id.empty() || m_device_number.empty()) {
/*
    if (GetDeviceData()) {
      m_settings->SetSetting("deviceid", m_device_id);
      m_settings->SetSetting("devicenumber", m_device_number);
      m_settings->SetSetting("deviceserial", m_device_serial);
    }
*/
    if (m_device_serial.empty()) {
      m_device_serial = m_httpClient->GetUUID();
      m_settings->SetSetting("deviceserial", m_device_serial);
      kodi::Log(ADDON_LOG_DEBUG, "Generated Device Serial: %s", m_device_serial.c_str());
    }
    if (GetDeviceFromSerial()) {
      m_settings->SetSetting("deviceid", m_device_id);
      m_settings->SetSetting("devicenumber", m_device_number);
    }
  } else {
    kodi::Log(ADDON_LOG_INFO, "Using stored device registration. deviceId=%s deviceNumber=%s",
              DescribeValue(m_device_id).c_str(),
              DescribeValue(m_device_number).c_str());
  }

  bool allgood = true;
  bool retried_with_fresh_device = false;

  m_subscriber_id = m_settings->GetEonSubscriberID();
  if (m_subscriber_id.empty()) {
    const bool households_ok = GetHouseholds();
    kodi::Log(ADDON_LOG_INFO, "Startup step GetHouseholds=%s subscriberId=%s",
              BoolState(households_ok), DescribeValue(m_subscriber_id).c_str());
    if (households_ok) {
      m_settings->SetSetting("subscriberid", m_subscriber_id);
    } else if (m_platform == PLATFORM_WEB && !m_device_number.empty()) {
      kodi::Log(ADDON_LOG_INFO, "Retrying startup after refreshing stored web device registration.");
      retried_with_fresh_device = RefreshDeviceRegistration();
      if (retried_with_fresh_device && GetHouseholds()) {
        kodi::Log(ADDON_LOG_INFO, "Startup retry GetHouseholds=true subscriberId=%s",
                  DescribeValue(m_subscriber_id).c_str());
        m_settings->SetSetting("subscriberid", m_subscriber_id);
      } else {
        allgood = false;
      }
    } else {
      allgood = false;
    }
  } else {
    kodi::Log(ADDON_LOG_INFO, "Using stored subscriber ID. subscriberId=%s",
              DescribeValue(m_subscriber_id).c_str());
  }

  if (m_service_provider.empty() || m_support_web.empty()) {
    allgood = GetServiceProvider();
    kodi::Log(ADDON_LOG_INFO, "Startup step GetServiceProvider=%s serviceProvider=%s supportApi=%s",
              BoolState(allgood), DescribeValue(m_service_provider).c_str(),
              DescribeValue(m_support_web).c_str());
    if (!allgood && m_platform == PLATFORM_WEB && !retried_with_fresh_device && !m_device_number.empty()) {
      kodi::Log(ADDON_LOG_INFO, "Retrying service provider lookup after refreshing stored web device registration.");
      retried_with_fresh_device = RefreshDeviceRegistration();
      if (retried_with_fresh_device)
      {
        allgood = GetServiceProvider();
        kodi::Log(ADDON_LOG_INFO, "Startup retry GetServiceProvider=%s serviceProvider=%s supportApi=%s",
                  BoolState(allgood), DescribeValue(m_service_provider).c_str(),
                  DescribeValue(m_support_web).c_str());
      }
    }
  }

  if (allgood) {
    allgood = GetRenderingProfiles();
    kodi::Log(ADDON_LOG_INFO, "Startup step GetRenderingProfiles=%s totalProfiles=%zu",
              BoolState(allgood), m_rendering_profiles.size());
  }
  if (allgood) {
    allgood = GetServers();
    kodi::Log(ADDON_LOG_INFO, "Startup step GetServers=%s liveServers=%zu timeshiftServers=%zu",
              BoolState(allgood), m_live_servers.size(), m_timeshift_servers.size());
  }
  if (m_settings->IsTVenabled() && (allgood)) {
    allgood = GetCategories(false);
    kodi::Log(ADDON_LOG_INFO, "Startup step GetCategories(TV)=%s totalCategories=%zu",
              BoolState(allgood), m_categories.size());
    allgood = LoadChannels(false);
  }
  if (m_settings->IsRadioenabled() && (allgood)) {
    allgood = GetCategories(true);
    kodi::Log(ADDON_LOG_INFO, "Startup step GetCategories(Radio)=%s totalCategories=%zu",
              BoolState(allgood), m_categories.size());
    allgood = LoadChannels(true);
  }

  const size_t tv_channels = std::count_if(m_channels.begin(), m_channels.end(),
                                           [](const EonChannel& channel) { return !channel.bRadio; });
  const size_t radio_channels = std::count_if(m_channels.begin(), m_channels.end(),
                                              [](const EonChannel& channel) { return channel.bRadio; });
  kodi::Log(ADDON_LOG_INFO,
            "Startup finished. allgood=%s totalChannels=%zu tvChannels=%zu radioChannels=%zu categories=%zu",
            BoolState(allgood), m_channels.size(), tv_channels, radio_channels, m_categories.size());
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
  kodi::Log(ADDON_LOG_DEBUG, "Load Eon Channels. type=%s", isRadio ? "radio" : "tv");

  std::string url = m_api + "v3/channels?channelType=" + (isRadio ? "RADIO&channelSort=RECOMMENDED&sortDir=DESC" : "TV");

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to load channels");
    return false;
  }

  int currentnumber = 0;
  int startnumber = m_settings->GetStartNum()-1;
  int lastnumber = startnumber;
  const rapidjson::Value& channels = doc;
  size_t total_channels = 0;
  size_t added_channels = 0;
  size_t unsubscribed_skipped = 0;
  size_t archive_channels = 0;

  for (rapidjson::Value::ConstValueIterator itr1 = channels.Begin();
      itr1 != channels.End(); ++itr1)
  {
    ++total_channels;
    const rapidjson::Value& channelItem = (*itr1);

    std::string channame;
    if (m_settings->UseShortNames()) {
      channame = Utils::JsonStringOrEmpty(channelItem, "shortName");
    } else {
      channame = Utils::JsonStringOrEmpty(channelItem, "name");
    }

    EonChannel eon_channel;

    eon_channel.bRadio = isRadio;
    eon_channel.bArchive = Utils::JsonBoolOrFalse(channelItem, "cutvEnabled");
    if (eon_channel.bArchive)
      ++archive_channels;
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
    int ageRating = 0;
    try {
      ageRating = std::stoi(Utils::JsonStringOrEmpty(channelItem,"ageRating"));
    } catch (std::invalid_argument&e) {

    }
    eon_channel.ageRating = ageRating;
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
      kodi::Log(ADDON_LOG_DEBUG, "%i. Channel Name: %s ID: %i Sig: %s", lastnumber, channame.c_str(), ref_id, eon_channel.sig.c_str());
      m_channels.emplace_back(eon_channel);
      ++added_channels;
    } else {
      ++unsubscribed_skipped;
    }
  }

  kodi::Log(ADDON_LOG_INFO,
            "LoadChannels summary. type=%s total=%zu added=%zu skippedUnsubscribed=%zu archiveEnabled=%zu totalStored=%zu",
            isRadio ? "radio" : "tv",
            total_channels,
            added_channels,
            unsubscribed_skipped,
            archive_channels,
            m_channels.size());

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

  std::string url = m_api + "v1/events";

  rapidjson::Document doc;
  if (!GetPostJson(url, postData, doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to register event");
    return false;
  }

  return Utils::JsonBoolOrFalse(doc, "success");
}

void CPVREon::SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                                  const std::string& url,
                                  const bool& realtime,
                                  const bool& playTimeshiftBuffer,
                                  const bool& isLive,
                                  time_t starttime,
                                  time_t endtime)
{
  kodi::Log(ADDON_LOG_DEBUG,
            "[PLAY STREAM] url=%s realtime=%s playTimeshiftBuffer=%s mode=%s start=%lld end=%lld",
            url.c_str(),
            BoolState(realtime),
            BoolState(playTimeshiftBuffer),
            isLive ? "live" : "replay",
            static_cast<long long>(starttime),
            static_cast<long long>(endtime));

  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, url);
  properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, realtime ? "true" : "false");

  int inputstream = m_settings->GetInputstream();

  if (inputstream == INPUTSTREAM_ADAPTIVE)
  {
    if (!Utils::CheckInputstreamInstalledAndEnabled("inputstream.adaptive"))
    {
      kodi::Log(ADDON_LOG_DEBUG, "inputstream.adaptive selected but not installed or enabled");
      return;
    }
    kodi::Log(ADDON_LOG_DEBUG, "...using inputstream.adaptive");
    properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
    properties.emplace_back("inputstream.adaptive.manifest_type", "hls");
    //  properties.emplace_back("inputstream.adaptive.original_audio_language", "bs");
    //  properties.emplace_back("inputstream.adaptive.stream_selection_type", "adaptive");
    // properties.emplace_back("inputstream.adaptive.stream_selection_type", "manual-osd");
    properties.emplace_back("inputstream.adaptive.stream_selection_type", "fixed-res");
    properties.emplace_back("inputstream.adaptive.chooser_resolution_max", "4K");
    //properties.emplace_back("inputstream.adaptive.stream_selection_type", "fixed-res");
    properties.emplace_back("inputstream.adaptive.manifest_headers", "User-Agent=" + EonParameters[m_platform].user_agent);
    // properties.emplace_back("inputstream.adaptive.manifest_update_parameter", "full");
  } else if (inputstream == INPUTSTREAM_FFMPEGDIRECT)
  {
    if (!Utils::CheckInputstreamInstalledAndEnabled("inputstream.ffmpegdirect"))
    {
      kodi::Log(ADDON_LOG_DEBUG, "inputstream.ffmpegdirect selected but not installed or enabled");
      return;
    }
    kodi::Log(ADDON_LOG_DEBUG, "...using inputstream.ffmpegdirect");
    properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.ffmpegdirect");
    properties.emplace_back("inputstream.ffmpegdirect.manifest_type", "hls");
    properties.emplace_back("inputstream.ffmpegdirect.is_realtime_stream", realtime ? "true" : "false");
    if (isLive)
    {
      properties.emplace_back("inputstream.ffmpegdirect.stream_mode", "timeshift");
    }
    else
    {
      properties.emplace_back("inputstream.ffmpegdirect.open_mode", "ffmpeg");
      properties.emplace_back("inputstream.ffmpegdirect.playback_as_live", "false");
      kodi::Log(ADDON_LOG_INFO,
                "Using simplified ffmpegdirect replay mode. start=%lld end=%lld",
                static_cast<long long>(starttime),
                static_cast<long long>(endtime));
    }
  } else {
    kodi::Log(ADDON_LOG_DEBUG, "Unknown inputstream detected");
  }
  properties.emplace_back(PVR_STREAM_PROPERTY_MIMETYPE, "application/x-mpegURL");

  if (!isLive)
  {
    kodi::Log(ADDON_LOG_INFO,
              "Replay properties prepared. inputstream=%s realtime=%s start=%lld end=%lld urlLen=%zu",
              InputstreamName(inputstream),
              BoolState(realtime),
              static_cast<long long>(starttime),
              static_cast<long long>(endtime),
              url.size());
  }
}

bool CPVREon::BuildPlaybackUrl(const EonChannel& channel,
                               time_t starttime,
                               time_t endtime,
                               const bool& isLive,
                               EonPlaybackUrlResult& result,
                               const bool includeDiagnostics)
{
  result = {};

  if (channel.publishingPoints.empty())
  {
    kodi::Log(ADDON_LOG_ERROR, "Channel uid=%i has no publishing points", channel.iUniqueId);
    return false;
  }

  std::string streaming_profile = "hp7000";

  unsigned int rndbitrate = 0;
  unsigned int current_bitrate = 0;
  unsigned int current_id = 0;
  for (unsigned int i = 0; i < channel.publishingPoints[0].profileIds.size(); i++)
  {
    current_bitrate = getBitrate(channel.bRadio, channel.publishingPoints[0].profileIds[i]);
    kodi::Log(ADDON_LOG_DEBUG, "Bitrate is: %u for profile id: %u", current_bitrate,
              channel.publishingPoints[0].profileIds[i]);
    if (current_bitrate > rndbitrate)
    {
      current_id = channel.publishingPoints[0].profileIds[i];
      rndbitrate = current_bitrate;
    }
  }

  if (current_id == 0)
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to resolve rendering profile for channel uid=%i",
              channel.iUniqueId);
    return false;
  }

  streaming_profile = getCoreStreamId(current_id);
  result.streamProfile = streaming_profile;
  result.bitrate = static_cast<int>(rndbitrate);
  kodi::Log(ADDON_LOG_DEBUG, "Channel Rendering Profile -> %u", current_id);

  m_session_id = Utils::CreateUUID();

  EonServer currentServer;
  if (!GetServer(isLive, currentServer))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to select %s server for channel uid=%i",
              isLive ? "live" : "timeshift", channel.iUniqueId);
    return false;
  }

  std::string plain_aes;
  const bool use_adaptive_stream_hint =
      isLive || m_settings->GetInputstream() == INPUTSTREAM_ADAPTIVE;

  if (m_platform == PLATFORM_ANDROIDTV)
  {
    plain_aes = "channel=" + channel.publishingPoints[0].publishingPoint + ";" +
                "stream=" + streaming_profile + ";" + "sp=" + m_service_provider + ";" +
                "u=" + m_settings->GetEonStreamUser() + ";" + "m=" + currentServer.ip + ";" +
                "device=" + m_settings->GetEonDeviceNumber() + ";" + "ctime=" + GetTime() + ";" +
                "lang=eng;player=" + PLAYER + ";" +
                "aa=" + (channel.aaEnabled ? "true" : "false") + ";" +
                "conn=" + CONN_TYPE_ETHERNET + ";" + "minvbr=100;" +
                "ss=" + m_settings->GetEonStreamKey() + ";" + "session=" + m_session_id + ";" +
                "maxvbr=" + std::to_string(rndbitrate);
    if (!isLive)
      plain_aes = plain_aes + ";t=" + std::to_string(static_cast<int>(starttime)) + "000;";
  }
  else
  {
    plain_aes = "channel=" + channel.publishingPoints[0].publishingPoint + ";" +
                "stream=" + streaming_profile + ";" + "sp=" + m_service_provider + ";" +
                "u=" + m_settings->GetEonStreamUser() + ";" +
                "ss=" + m_settings->GetEonStreamKey() + ";" + "minvbr=100;" +
                "sig=" + channel.sig + ";" + "session=" + m_session_id + ";" +
                "m=" + currentServer.ip + ";" + "device=" + m_settings->GetEonDeviceNumber() +
                ";" + "ctime=" + GetTime() + ";" + "conn=" + CONN_TYPE_BROWSER + ";";
    if (use_adaptive_stream_hint)
      plain_aes = plain_aes + "adaptive=true;";
    plain_aes = plain_aes + "player=" + PLAYER + ";";
    if (!isLive)
      plain_aes = plain_aes + "t=" + std::to_string(static_cast<int>(starttime)) + "000;";
    plain_aes = plain_aes + "aa=" + (channel.aaEnabled ? "true" : "false");
  }

  if (!isLive)
  {
    kodi::Log(ADDON_LOG_INFO,
              "Replay URL payload prepared. inputstream=%s adaptiveHint=%s start=%lld end=%lld",
              InputstreamName(m_settings->GetInputstream()),
              BoolState(use_adaptive_stream_hint),
              static_cast<long long>(starttime),
              static_cast<long long>(endtime));
  }

  std::string key = base64_decode(urlsafedecode(m_settings->GetEonStreamKey()));

  std::string iv_str;
  iv_str.reserve(block_size);
  for (int i = 0; i < block_size; i++)
    iv_str.push_back(static_cast<char>(rand() & 0xFF));

  std::string enc_str = aes_encrypt_cbc(iv_str, key, plain_aes);

  kodi::Log(ADDON_LOG_DEBUG, "IV -> %s", string_to_hex(iv_str).c_str());
  kodi::Log(ADDON_LOG_DEBUG, "IV (base64) -> %s",
            urlsafeencode(base64_encode(iv_str.c_str(), iv_str.length())).c_str());
  kodi::Log(ADDON_LOG_DEBUG, "Encrypted -> %s", string_to_hex(enc_str).c_str());
  kodi::Log(ADDON_LOG_DEBUG, "Encrypted (base64) -> %s",
            urlsafeencode(base64_encode(enc_str.c_str(), enc_str.length())).c_str());

  result.url = "https://" + currentServer.hostname +
               "/stream?i=" + urlsafeencode(base64_encode(iv_str.c_str(), iv_str.length())) +
               "&a=" + urlsafeencode(base64_encode(enc_str.c_str(), enc_str.length()));
  if (m_platform == PLATFORM_ANDROIDTV)
    result.url = result.url + "&lang=eng";
  result.url = result.url + "&sp=" + m_service_provider + "&u=" + m_settings->GetEonStreamUser() +
               "&player=" + PLAYER + "&session=" + m_session_id;
  if (m_platform != PLATFORM_ANDROIDTV)
    result.url = result.url + "&sig=" + channel.sig;

  kodi::Log(ADDON_LOG_DEBUG, "Encrypted Stream URL -> %s", result.url.c_str());

  if (includeDiagnostics && !isLive)
  {
    Curl manifestCurl;
    manifestCurl.AddHeader("User-Agent", EonParameters[m_platform].user_agent);
    int manifestStatus = 0;
    const std::string manifestBody = manifestCurl.Get(result.url, manifestStatus);
    kodi::Log(ADDON_LOG_INFO,
              "Replay manifest fetch. status=%i bodyLen=%zu extinf=%zu endlist=%s vod=%s event=%s preview=%s",
              manifestStatus, manifestBody.size(), CountOccurrences(manifestBody, "#EXTINF"),
              BoolState(manifestBody.find("#EXT-X-ENDLIST") != std::string::npos),
              BoolState(manifestBody.find("#EXT-X-PLAYLIST-TYPE:VOD") != std::string::npos),
              BoolState(manifestBody.find("#EXT-X-PLAYLIST-TYPE:EVENT") != std::string::npos),
              PreviewForLog(manifestBody).c_str());

    const std::string variantUrl = FirstVariantPlaylistUrl(manifestBody, result.url);
    if (!variantUrl.empty())
    {
      int variantStatus = 0;
      const std::string variantBody = manifestCurl.Get(variantUrl, variantStatus);
      kodi::Log(ADDON_LOG_INFO,
                "Replay variant fetch. status=%i bodyLen=%zu extinf=%zu endlist=%s vod=%s event=%s preview=%s",
                variantStatus, variantBody.size(), CountOccurrences(variantBody, "#EXTINF"),
                BoolState(variantBody.find("#EXT-X-ENDLIST") != std::string::npos),
                BoolState(variantBody.find("#EXT-X-PLAYLIST-TYPE:VOD") != std::string::npos),
                BoolState(variantBody.find("#EXT-X-PLAYLIST-TYPE:EVENT") != std::string::npos),
                PreviewForLog(variantBody).c_str());
    }
  }

  return true;
}

bool CPVREon::UseExperimentalNativeStream() const
{
  return m_settings->UseExperimentalNativeStream() && m_platform == PLATFORM_WEB;
}

bool CPVREon::OpenNativeStream(const EonChannel& channel,
                               bool isLive,
                               time_t starttime,
                               time_t endtime,
                               time_t initialPlaybackTime,
                               bool liveEdge)
{
  CloseNativeStreamInternal();

  const time_t effectivePlaybackTime =
      isLive ? time(nullptr)
             : std::clamp(initialPlaybackTime > 0 ? initialPlaybackTime : starttime, starttime,
                          std::max(starttime, endtime - 1));

  EonPlaybackUrlResult playback;
  if (!BuildPlaybackUrl(channel, effectivePlaybackTime, endtime, isLive, playback, false))
    return false;

  m_nativeStream.open = true;
  m_nativeStream.isLive = isLive;
  m_nativeStream.seekable = !isLive;
  m_nativeStream.liveEdge = liveEdge;
  m_nativeStream.startupTimelineReady = false;
  m_nativeStream.ignoreInitialArchiveSeeks =
      !isLive && !liveEdge && effectivePlaybackTime <= starttime;
  m_nativeStream.channel = channel;
  m_nativeStream.programmeStartTime = isLive ? time(nullptr) : starttime;
  m_nativeStream.programmeEndTime = isLive ? 0 : endtime;
  m_nativeStream.sessionStartTime = effectivePlaybackTime;
  m_nativeStream.openMonotonicMs = MonotonicNowMs();
  m_nativeStream.sessionAnchorMonotonicMs = MonotonicNowMs();
  m_nativeStream.masterUrl = playback.url;
  m_nativeStream.bitrate = playback.bitrate;
  m_nativeStream.virtualUnitsPerSecond = NATIVE_VIRTUAL_UNITS_PER_SECOND;

  if (!isLive)
  {
    const int64_t duration =
        std::max<int64_t>(m_nativeStream.programmeEndTime - m_nativeStream.programmeStartTime, 1);
    m_nativeStream.virtualLength = duration * m_nativeStream.virtualUnitsPerSecond;
    m_nativeStream.currentPosition = TimeToStreamPosition(effectivePlaybackTime);
  }

  kodi::Log(ADDON_LOG_INFO,
            "Opening native %s stream. channel=%s uid=%i programmeStart=%lld programmeEnd=%lld playbackStart=%lld bitrate=%i unitsPerSecond=%lld",
            isLive ? "live" : "archive",
            channel.strChannelName.c_str(),
            channel.iUniqueId,
            static_cast<long long>(starttime),
            static_cast<long long>(endtime),
            static_cast<long long>(effectivePlaybackTime),
            playback.bitrate,
            static_cast<long long>(m_nativeStream.virtualUnitsPerSecond));

  if (!UpdateNativeVariantUrl(true) || !LoadNextNativeFragment())
  {
    CloseNativeStreamInternal();
    return false;
  }

  return true;
}

void CPVREon::CloseNativeStreamInternal()
{
  if (m_nativeStream.open)
  {
    const int64_t visiblePosition =
        m_nativeStream.isLive ? 0 : GetCurrentNativePosition();
    kodi::Log(ADDON_LOG_INFO,
              "Closing native stream. live=%s currentPosition=%lld queuedFragments=%zu",
              BoolState(m_nativeStream.isLive),
              static_cast<long long>(visiblePosition),
              m_nativeStream.pendingFragments.size());
  }

  m_nativeStream = {};
}

bool CPVREon::RestartNativeStreamAt(time_t starttime)
{
  if (!m_nativeStream.open || m_nativeStream.isLive)
    return false;

  if (m_nativeStream.programmeEndTime <= m_nativeStream.programmeStartTime)
    return false;

  const time_t seekableEndTime = GetCurrentNativeSeekableEndTime();
  const time_t clampedStart = std::clamp(
      starttime, m_nativeStream.programmeStartTime,
      std::max(m_nativeStream.programmeStartTime, seekableEndTime - 1));
  const time_t currentPlaybackTime = StreamPositionToTime(GetCurrentNativePosition());
  long long restartDelta =
      static_cast<long long>(clampedStart) - static_cast<long long>(currentPlaybackTime);
  if (restartDelta < 0)
    restartDelta = -restartDelta;
  if (restartDelta <= static_cast<long long>(NATIVE_SEEK_RESTART_EPSILON_SECONDS))
  {
    m_nativeStream.currentPosition = TimeToStreamPosition(clampedStart);
    kodi::Log(ADDON_LOG_DEBUG,
              "Skipping native archive restart. currentTime=%lld targetTime=%lld delta=%lld",
              static_cast<long long>(currentPlaybackTime),
              static_cast<long long>(clampedStart),
              restartDelta);
    return true;
  }

  EonPlaybackUrlResult playback;
  if (!BuildPlaybackUrl(m_nativeStream.channel, clampedStart, m_nativeStream.programmeEndTime,
                        false, playback, false))
  {
    return false;
  }

  m_nativeStream.sessionStartTime = clampedStart;
  m_nativeStream.masterUrl = playback.url;
  m_nativeStream.variantUrl.clear();
  m_nativeStream.pendingFragments.clear();
  m_nativeStream.lastFragmentUrl.clear();
  m_nativeStream.currentFragmentData.clear();
  m_nativeStream.currentFragmentOffset = 0;
  m_nativeStream.sessionAnchorMonotonicMs = MonotonicNowMs();
  m_nativeStream.bitrate = playback.bitrate;
  m_nativeStream.currentPosition = TimeToStreamPosition(clampedStart);

  kodi::Log(ADDON_LOG_INFO,
            "Restarting native archive stream. channel=%s uid=%i targetTime=%lld position=%lld",
            m_nativeStream.channel.strChannelName.c_str(),
            m_nativeStream.channel.iUniqueId,
            static_cast<long long>(clampedStart),
            static_cast<long long>(m_nativeStream.currentPosition));

  return UpdateNativeVariantUrl(true) && LoadNextNativeFragment();
}

bool CPVREon::UpdateNativeVariantUrl(bool logErrors)
{
  if (!m_nativeStream.open || m_nativeStream.masterUrl.empty())
    return false;

  Curl manifestCurl;
  manifestCurl.AddHeader("User-Agent", EonParameters[m_platform].user_agent);
  int manifestStatus = 0;
  const std::string manifestBody = manifestCurl.Get(m_nativeStream.masterUrl, manifestStatus);
  if (manifestStatus != 200 && manifestStatus != 206)
  {
    if (logErrors)
    {
      kodi::Log(ADDON_LOG_ERROR, "Native stream manifest request failed. status=%i url=%s",
                manifestStatus, m_nativeStream.masterUrl.c_str());
    }
    return false;
  }

  std::string variantUrl = FirstVariantPlaylistUrl(manifestBody, m_nativeStream.masterUrl);
  if (variantUrl.empty())
    variantUrl = m_nativeStream.masterUrl;

  m_nativeStream.variantUrl = variantUrl;
  kodi::Log(ADDON_LOG_INFO, "Native stream variant selected. url=%s",
            m_nativeStream.variantUrl.c_str());
  return true;
}

bool CPVREon::PollNativeFragmentQueue(bool forceRefresh)
{
  if (!m_nativeStream.open)
    return false;

  if (!forceRefresh && !m_nativeStream.pendingFragments.empty())
    return true;

  if (m_nativeStream.variantUrl.empty() && !UpdateNativeVariantUrl(true))
    return false;

  Curl playlistCurl;
  playlistCurl.AddHeader("User-Agent", EonParameters[m_platform].user_agent);
  int playlistStatus = 0;
  const std::string playlistBody = playlistCurl.Get(m_nativeStream.variantUrl, playlistStatus);
  if (playlistStatus != 200 && playlistStatus != 206)
  {
    kodi::Log(ADDON_LOG_ERROR, "Native playlist request failed. status=%i url=%s", playlistStatus,
              m_nativeStream.variantUrl.c_str());
    return false;
  }

  if (CountOccurrences(playlistBody, "#EXTINF") == 0 &&
      playlistBody.find("#EXT-X-STREAM-INF") != std::string::npos)
  {
    const std::string nestedVariant = FirstVariantPlaylistUrl(playlistBody, m_nativeStream.variantUrl);
    if (!nestedVariant.empty() && nestedVariant != m_nativeStream.variantUrl)
    {
      m_nativeStream.variantUrl = nestedVariant;
      return PollNativeFragmentQueue(true);
    }
  }

  const std::vector<std::string> fragmentUrls =
      ExtractMediaSegmentUrls(playlistBody, m_nativeStream.variantUrl);
  if (fragmentUrls.empty())
  {
    kodi::Log(ADDON_LOG_ERROR, "Native playlist contained no fragments. url=%s preview=%s",
              m_nativeStream.variantUrl.c_str(), PreviewForLog(playlistBody).c_str());
    return false;
  }

  size_t startIndex = 0;
  if (!m_nativeStream.lastFragmentUrl.empty())
  {
    const auto lastIt =
        std::find(fragmentUrls.begin(), fragmentUrls.end(), m_nativeStream.lastFragmentUrl);
    if (lastIt != fragmentUrls.end())
      startIndex = static_cast<size_t>(std::distance(fragmentUrls.begin(), lastIt) + 1);
  }

  size_t added = 0;
  for (size_t i = startIndex; i < fragmentUrls.size(); ++i)
  {
    if (std::find(m_nativeStream.pendingFragments.begin(), m_nativeStream.pendingFragments.end(),
                  fragmentUrls[i]) != m_nativeStream.pendingFragments.end())
    {
      continue;
    }

    m_nativeStream.pendingFragments.push_back(fragmentUrls[i]);
    ++added;
  }

  kodi::Log(ADDON_LOG_DEBUG,
            "Native playlist poll. fragments=%zu added=%zu queued=%zu last=%s preview=%s",
            fragmentUrls.size(),
            added,
            m_nativeStream.pendingFragments.size(),
            m_nativeStream.lastFragmentUrl.empty() ? "<none>" : m_nativeStream.lastFragmentUrl.c_str(),
            PreviewForLog(playlistBody).c_str());

  return !m_nativeStream.pendingFragments.empty();
}

bool CPVREon::LoadNextNativeFragment()
{
  m_nativeStream.currentFragmentData.clear();
  m_nativeStream.currentFragmentOffset = 0;

  for (int attempt = 0; attempt < NATIVE_POLL_RETRY_COUNT; ++attempt)
  {
    if (!m_nativeStream.pendingFragments.empty())
      break;

    if (PollNativeFragmentQueue(true))
      break;

    std::this_thread::sleep_for(NATIVE_POLL_RETRY_DELAY);
  }

  while (!m_nativeStream.pendingFragments.empty())
  {
    const std::string fragmentUrl = m_nativeStream.pendingFragments.front();
    m_nativeStream.pendingFragments.pop_front();

    std::vector<uint8_t> data;
    int statusCode = 0;
    if (!FetchBinaryUrl(fragmentUrl, data, statusCode) || data.empty())
    {
      kodi::Log(ADDON_LOG_ERROR,
                "Failed to fetch native fragment. status=%i bytes=%zu url=%s",
                statusCode,
                data.size(),
                fragmentUrl.c_str());
      continue;
    }

    m_nativeStream.lastFragmentUrl = fragmentUrl;
    m_nativeStream.currentFragmentData = std::move(data);
    kodi::Log(ADDON_LOG_DEBUG, "Loaded native fragment. bytes=%zu url=%s",
              m_nativeStream.currentFragmentData.size(), fragmentUrl.c_str());
    return true;
  }

  return false;
}

bool CPVREon::FetchBinaryUrl(const std::string& url, std::vector<uint8_t>& data, int& statusCode)
{
  data.clear();
  statusCode = -1;

  kodi::vfs::CFile file;
  if (!file.CURLCreate(url))
  {
    kodi::Log(ADDON_LOG_ERROR, "CURLCreate failed for native fragment %s", url.c_str());
    return false;
  }

  file.CURLAddOption(ADDON_CURL_OPTION_HEADER, "User-Agent", EonParameters[m_platform].user_agent);
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "failonerror", "false");
  if (!file.CURLOpen(ADDON_READ_NO_CACHE))
  {
    kodi::Log(ADDON_LOG_ERROR, "CURLOpen failed for native fragment %s", url.c_str());
    return false;
  }

  const std::string proto = file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_PROTOCOL, "");
  const std::string::size_type posResponseCode = proto.find(' ');
  if (posResponseCode != std::string::npos)
    statusCode = atoi(proto.c_str() + (posResponseCode + 1));

  std::array<uint8_t, 32768> buffer{};
  ssize_t bytesRead = 0;
  while ((bytesRead = file.Read(buffer.data(), buffer.size())) > 0)
  {
    data.insert(data.end(), buffer.begin(), buffer.begin() + bytesRead);
  }

  return statusCode == 200 || statusCode == 206;
}

int64_t CPVREon::GetCurrentNativePosition() const
{
  if (!m_nativeStream.open || m_nativeStream.isLive || m_nativeStream.virtualLength <= 0)
    return 0;

  const int64_t basePosition = TimeToStreamPosition(m_nativeStream.sessionStartTime);
  const int64_t maxSeekablePosition = TimeToStreamPosition(GetCurrentNativeSeekableEndTime());
  if (m_nativeStream.sessionAnchorMonotonicMs <= 0 || m_nativeStream.virtualUnitsPerSecond <= 0)
  {
    return std::clamp<int64_t>(std::max(m_nativeStream.currentPosition, basePosition), 0,
                               maxSeekablePosition);
  }

  const int64_t elapsedMs =
      std::max<int64_t>(MonotonicNowMs() - m_nativeStream.sessionAnchorMonotonicMs, 0);
  const int64_t elapsedUnits =
      (elapsedMs * m_nativeStream.virtualUnitsPerSecond) / 1000;
  return std::clamp<int64_t>(
      std::max(m_nativeStream.currentPosition, basePosition + elapsedUnits), 0, maxSeekablePosition);
}

time_t CPVREon::GetCurrentNativeSeekableEndTime() const
{
  if (!m_nativeStream.open || m_nativeStream.isLive)
    return m_nativeStream.sessionStartTime;

  if (m_nativeStream.programmeEndTime <= m_nativeStream.programmeStartTime)
    return m_nativeStream.programmeStartTime;

  const time_t now = time(nullptr);
  const time_t delayedNow =
      now > NATIVE_LIVE_EDGE_DELAY_SECONDS ? now - NATIVE_LIVE_EDGE_DELAY_SECONDS : now;
  return std::clamp(delayedNow, m_nativeStream.programmeStartTime, m_nativeStream.programmeEndTime);
}

time_t CPVREon::StreamPositionToTime(int64_t position) const
{
  if (!m_nativeStream.open || m_nativeStream.isLive || m_nativeStream.virtualLength <= 0 ||
      m_nativeStream.virtualUnitsPerSecond <= 0)
  {
    return m_nativeStream.sessionStartTime;
  }

  const int64_t clampedPosition = std::clamp<int64_t>(position, 0, m_nativeStream.virtualLength);
  const int64_t offsetSeconds = clampedPosition / m_nativeStream.virtualUnitsPerSecond;
  return m_nativeStream.programmeStartTime + offsetSeconds;
}

int64_t CPVREon::TimeToStreamPosition(time_t timeValue) const
{
  if (!m_nativeStream.open || m_nativeStream.isLive || m_nativeStream.virtualLength <= 0 ||
      m_nativeStream.virtualUnitsPerSecond <= 0)
  {
    return 0;
  }

  const time_t clampedTime =
      std::clamp(timeValue, m_nativeStream.programmeStartTime, m_nativeStream.programmeEndTime);
  const int64_t offsetSeconds = clampedTime - m_nativeStream.programmeStartTime;
  return std::clamp<int64_t>(offsetSeconds * m_nativeStream.virtualUnitsPerSecond, 0,
                             m_nativeStream.virtualLength);
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
  capabilities.SetHandlesInputStream(UseExperimentalNativeStream());

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetBackendName(std::string& name)
{
  name = "EON PVR";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetBackendVersion(std::string& version)
{
  version = STR(EON_VERSION);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetConnectionString(std::string& connection)
{
  connection = "connected";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetBackendHostname(std::string& hostname)
{
  hostname = m_api;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  total = 0;
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

    kodi::Log(ADDON_LOG_DEBUG, "EPG Request for Channel %u Start %u End %u", channel.iUniqueId, start, end);

    std::string url = m_api + "v1/events/epg" +
                              "?cid=" + std::to_string(channel.iUniqueId) +
                              "&fromTime=" + std::to_string(start) + "000" +
                              "&toTime=" + std::to_string(end) + "000";

    rapidjson::Document epgDoc;
    if (!GetPostJson(url, "", epgDoc)) {
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
      unsigned int epg_tag_flags = EPG_TAG_FLAG_UNDEFINED;

      tag.SetUniqueBroadcastId(Utils::JsonIntOrZero(epgItem,"id"));
      tag.SetUniqueChannelId(channelUid);
      tag.SetTitle(Utils::JsonStringOrEmpty(epgItem,"title"));
      tag.SetOriginalTitle(Utils::JsonStringOrEmpty(epgItem,"originalTitle"));
      time_t starttime = (time_t) (Utils::JsonInt64OrZero(epgItem,"startTime") / 1000);
      time_t endtime = (time_t) (Utils::JsonInt64OrZero(epgItem,"endTime") / 1000);
      tag.SetStartTime(starttime);
      tag.SetEndTime(endtime);
      tag.SetPlot(Utils::JsonStringOrEmpty(epgItem,"shortDescription"));
      int seasonNumber = Utils::JsonIntOrZero(epgItem,"seasonNumber");
      if (seasonNumber != 0)
        tag.SetSeriesNumber(seasonNumber);
      int episodeNumber = Utils::JsonIntOrZero(epgItem,"episodeNumber");
      if (episodeNumber != 0)
      {
        tag.SetEpisodeNumber(episodeNumber);
        epg_tag_flags += EPG_TAG_FLAG_IS_SERIES;
      }
      int ageRating = 0;
      try {
        ageRating = std::stoi(Utils::JsonStringOrEmpty(epgItem,"ageRating"));
      } catch (std::invalid_argument&e) {

      }
      if (ageRating != 0)
        tag.SetParentalRating(ageRating);

      if (Utils::JsonBoolOrFalse(epgItem, "liveBroadcast"))
        epg_tag_flags += EPG_TAG_FLAG_IS_LIVE;

      const rapidjson::Value& images = epgItem["images"];
      for (rapidjson::Value::ConstValueIterator itr2 = images.Begin();
          itr2 != images.End(); ++itr2)
      {
        const rapidjson::Value& imageItem = (*itr2);

        if (Utils::JsonStringOrEmpty(imageItem, "size") == "STB_XL") {
          tag.SetIconPath(m_images_api + Utils::JsonStringOrEmpty(imageItem, "path"));
        }
      }
/*
      const rapidjson::Value& categories = epgItem["categories"];
      for (rapidjson::SizeType i = 0; i < categories.Size(); i++)
      {
        kodi::Log(ADDON_LOG_DEBUG, "Category: %u", categories[i].GetInt());
      }
*/
//      kodi::Log(ADDON_LOG_DEBUG, "%u adding EPG: ID: %u Title: %s Start: %u End: %u", channelUid, Utils::JsonIntOrZero(epgItem,"id"), Utils::JsonStringOrEmpty(epgItem,"title").c_str(),Utils::JsonIntOrZero(epgItem,"startTime")/1000,Utils::JsonIntOrZero(epgItem,"endTime")/1000);
      tag.SetFlags(epg_tag_flags);
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
  kodi::Log(ADDON_LOG_INFO,
            "function call: [%s] channelUid=%u start=%lld end=%lld",
            __FUNCTION__,
            tag.GetUniqueChannelId(),
            static_cast<long long>(tag.GetStartTime()),
            static_cast<long long>(tag.GetEndTime()));
  for (const auto& channel : m_channels)
  {
    if (channel.iUniqueId == tag.GetUniqueChannelId())
    {
      if (UseExperimentalNativeStream())
      {
        const time_t now = time(nullptr);
        const bool isInProgress =
            now > tag.GetStartTime() && now < tag.GetEndTime();
        const time_t initialPlaybackTime = tag.GetStartTime();

        m_pendingPlayback.active = true;
        m_pendingPlayback.liveEdge = isInProgress;
        m_pendingPlayback.channelUid = channel.iUniqueId;
        m_pendingPlayback.startTime = tag.GetStartTime();
        m_pendingPlayback.endTime = tag.GetEndTime();
        m_pendingPlayback.initialPlaybackTime = initialPlaybackTime;
        m_pendingPlayback.requestTime = time(nullptr);
        properties.emplace_back(PVR_STREAM_PROPERTY_EPGPLAYBACKASLIVE, "true");
        kodi::Log(ADDON_LOG_INFO,
                  "Queued native EPG playback. channel=%s uid=%i mode=%s programmeStart=%lld programmeEnd=%lld initialPlayback=%lld",
                  channel.strChannelName.c_str(),
                  channel.iUniqueId,
                  isInProgress ? "archive-in-progress" : "archive",
                  static_cast<long long>(m_pendingPlayback.startTime),
                  static_cast<long long>(m_pendingPlayback.endTime),
                  static_cast<long long>(m_pendingPlayback.initialPlaybackTime));
        return PVR_ERROR_NO_ERROR;
      }

      return GetStreamProperties(channel, properties, tag.GetStartTime(), tag.GetEndTime(), false);
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

    int ageRating = m_settings->GetAgeRating();
    if (channel.bRadio == bRadio && ageRating >= channel.ageRating)
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
    const EonChannel& channel,
    std::vector<kodi::addon::PVRStreamProperty>& properties,
    time_t starttime,
    time_t endtime,
    const bool& isLive)
{
    kodi::Log(ADDON_LOG_DEBUG,
              "function call: [%s] channel=%s uid=%i mode=%s start=%lld end=%lld",
              __FUNCTION__,
              channel.strChannelName.c_str(),
              channel.iUniqueId,
              isLive ? "live" : "replay",
              static_cast<long long>(starttime),
              static_cast<long long>(endtime));
    EonPlaybackUrlResult playback;
    if (!BuildPlaybackUrl(channel, starttime, endtime, isLive, playback, true))
      return PVR_ERROR_SERVER_ERROR;

    SetStreamProperties(properties, playback.url, isLive, false, isLive, starttime, endtime);

    for (auto& prop : properties)
        kodi::Log(ADDON_LOG_DEBUG, "Name: %s Value: %s", prop.GetName().c_str(), prop.GetValue().c_str());

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetChannelStreamProperties(
    const kodi::addon::PVRChannel& channel,
    std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s] channelUid=%u", __FUNCTION__,
            channel.GetUniqueId());

  if (UseExperimentalNativeStream())
  {
    return PVR_ERROR_NO_ERROR;
  }

  EonChannel addonChannel;
  if (GetChannel(channel, addonChannel)) {
    if (addonChannel.subscribed) {
      return GetStreamProperties(addonChannel, properties, 0, 0, true);
    }
    kodi::Log(ADDON_LOG_DEBUG, "Channel not subscribed");
    return PVR_ERROR_SERVER_ERROR;
  }
  kodi::Log(ADDON_LOG_DEBUG, "Channel not found");
  return PVR_ERROR_SERVER_ERROR;
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
      kodi::Log(ADDON_LOG_DEBUG, "Group added: %s at position %u", it->name.c_str(), it->order);
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

bool CPVREon::OpenLiveStream(const kodi::addon::PVRChannel& channel)
{
  if (!UseExperimentalNativeStream())
    return false;

  EonChannel addonChannel;
  if (!GetChannel(channel, addonChannel) || !addonChannel.subscribed)
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to resolve native stream channel uid=%u", channel.GetUniqueId());
    return false;
  }

  const time_t now = time(nullptr);
  const bool usePendingArchive =
      m_pendingPlayback.active && m_pendingPlayback.channelUid == addonChannel.iUniqueId &&
      now - m_pendingPlayback.requestTime <= PENDING_PLAYBACK_TTL_SECONDS;

  const time_t archiveStart = usePendingArchive ? m_pendingPlayback.startTime : 0;
  const time_t archiveEnd = usePendingArchive ? m_pendingPlayback.endTime : 0;
  const time_t initialPlaybackTime =
      usePendingArchive ? m_pendingPlayback.initialPlaybackTime : 0;
  const bool liveEdge = usePendingArchive ? m_pendingPlayback.liveEdge : false;

  kodi::Log(ADDON_LOG_INFO,
            "OpenLiveStream in native mode. channel=%s uid=%i mode=%s programmeStart=%lld programmeEnd=%lld initialPlayback=%lld liveEdge=%s",
            addonChannel.strChannelName.c_str(),
            addonChannel.iUniqueId,
            usePendingArchive ? "archive" : "live",
            static_cast<long long>(archiveStart),
            static_cast<long long>(archiveEnd),
            static_cast<long long>(initialPlaybackTime),
            BoolState(liveEdge));

  m_pendingPlayback = {};
  return OpenNativeStream(addonChannel, !usePendingArchive, archiveStart, archiveEnd,
                          initialPlaybackTime, liveEdge);
}

void CPVREon::CloseLiveStream()
{
  if (UseExperimentalNativeStream())
    CloseNativeStreamInternal();
}

int CPVREon::ReadLiveStream(unsigned char* buffer, unsigned int size)
{
  if (!UseExperimentalNativeStream() || !m_nativeStream.open)
    return -1;

  unsigned int written = 0;
  while (written < size)
  {
    if (m_nativeStream.currentFragmentOffset >= m_nativeStream.currentFragmentData.size())
    {
      if (!LoadNextNativeFragment())
        break;
    }

    const size_t remainingFragment =
        m_nativeStream.currentFragmentData.size() - m_nativeStream.currentFragmentOffset;
    const size_t toCopy = std::min<size_t>(size - written, remainingFragment);
    memcpy(buffer + written,
           m_nativeStream.currentFragmentData.data() + m_nativeStream.currentFragmentOffset,
           toCopy);
    m_nativeStream.currentFragmentOffset += toCopy;
    written += static_cast<unsigned int>(toCopy);
  }

  if (!m_nativeStream.isLive)
    m_nativeStream.currentPosition = GetCurrentNativePosition();

  return static_cast<int>(written);
}

int64_t CPVREon::SeekLiveStream(int64_t position, int whence)
{
  if (!UseExperimentalNativeStream() || !m_nativeStream.open || !m_nativeStream.seekable)
    return -1;

  int64_t targetPosition = position;
  if (whence == SEEK_CUR)
    targetPosition = GetCurrentNativePosition() + position;
  else if (whence == SEEK_END)
    targetPosition = m_nativeStream.virtualLength + position;

  targetPosition = std::clamp<int64_t>(targetPosition, 0, m_nativeStream.virtualLength);
  const int64_t currentPosition = GetCurrentNativePosition();

  if (m_nativeStream.ignoreInitialArchiveSeeks && whence == SEEK_SET && targetPosition > 0)
  {
    const int64_t startupAgeMs =
        std::max<int64_t>(MonotonicNowMs() - m_nativeStream.openMonotonicMs, 0);
    if (startupAgeMs <= NATIVE_INITIAL_SEEK_IGNORE_WINDOW_MS)
    {
      kodi::Log(ADDON_LOG_INFO,
                "Ignoring initial Kodi archive seek. requested=%lld target=%lld current=%lld ageMs=%lld",
                static_cast<long long>(position),
                static_cast<long long>(targetPosition),
                static_cast<long long>(currentPosition),
                static_cast<long long>(startupAgeMs));
      return currentPosition;
    }

    m_nativeStream.ignoreInitialArchiveSeeks = false;
  }

  const time_t targetTime = StreamPositionToTime(targetPosition);
  kodi::Log(ADDON_LOG_INFO,
            "SeekLiveStream request. whence=%d requested=%lld target=%lld current=%lld targetTime=%lld",
            whence,
            static_cast<long long>(position),
            static_cast<long long>(targetPosition),
            static_cast<long long>(currentPosition),
            static_cast<long long>(targetTime));
  if (!RestartNativeStreamAt(targetTime))
    return -1;

  m_nativeStream.currentPosition = TimeToStreamPosition(targetTime);
  return m_nativeStream.currentPosition;
}

int64_t CPVREon::LengthLiveStream()
{
  if (!UseExperimentalNativeStream() || !m_nativeStream.open)
    return 0;

  if (!m_nativeStream.seekable)
    return 0;

  if (m_nativeStream.liveEdge)
    return TimeToStreamPosition(GetCurrentNativeSeekableEndTime());

  return m_nativeStream.virtualLength;
}

bool CPVREon::CanPauseStream()
{
  return UseExperimentalNativeStream() && m_nativeStream.open && !m_nativeStream.isLive;
}

bool CPVREon::CanSeekStream()
{
  return UseExperimentalNativeStream() && m_nativeStream.open && m_nativeStream.seekable;
}

bool CPVREon::IsRealTimeStream()
{
  if (!UseExperimentalNativeStream())
    return true;

  return !m_nativeStream.open || m_nativeStream.isLive;
}

PVR_ERROR CPVREon::GetStreamTimes(kodi::addon::PVRStreamTimes& times)
{
  if (!UseExperimentalNativeStream() || !m_nativeStream.open)
    return PVR_ERROR_NOT_IMPLEMENTED;

  if (m_nativeStream.isLive)
  {
    times.SetStartTime(time(nullptr));
    times.SetPTSStart(0);
    times.SetPTSBegin(0);
    times.SetPTSEnd(0);
    return PVR_ERROR_NO_ERROR;
  }

  const int64_t duration =
      std::max<int64_t>(m_nativeStream.programmeEndTime - m_nativeStream.programmeStartTime, 1);
  const int64_t visibleDuration =
      m_nativeStream.liveEdge
          ? std::max<int64_t>(GetCurrentNativeSeekableEndTime() - m_nativeStream.programmeStartTime,
                              1)
          : duration;

  if (!m_nativeStream.startupTimelineReady)
  {
    m_nativeStream.startupTimelineReady = true;
    if (m_nativeStream.ignoreInitialArchiveSeeks)
    {
      m_nativeStream.ignoreInitialArchiveSeeks = false;
      kodi::Log(ADDON_LOG_INFO,
                "Native archive startup timeline ready. Enabling archive seeks.");
    }
  }

  times.SetStartTime(m_nativeStream.programmeStartTime);
  times.SetPTSStart(0);
  times.SetPTSBegin(0);
  times.SetPTSEnd(visibleDuration * PVR_TIME_BASE);
  kodi::Log(ADDON_LOG_INFO,
            "GetStreamTimes archive. programmeStart=%lld programmeEnd=%lld sessionStart=%lld seekableEnd=%lld ptsStart=%lld ptsEnd=%lld",
            static_cast<long long>(m_nativeStream.programmeStartTime),
            static_cast<long long>(m_nativeStream.programmeEndTime),
            static_cast<long long>(m_nativeStream.sessionStartTime),
            static_cast<long long>(GetCurrentNativeSeekableEndTime()),
            0LL,
            static_cast<long long>(visibleDuration * PVR_TIME_BASE));
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

bool CPVREon::GetChannel(const kodi::addon::PVRChannel& channel, EonChannel& myChannel)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
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
  int target_server = 2;
  if (m_platform == PLATFORM_ANDROIDTV) {
    target_server = 3;
  }
  int count = 0;
  for (const auto& thisServer : servers)
  {
      count++;
      if (count == target_server) {
        myServer.id = thisServer.id;
        myServer.ip = thisServer.ip;
        myServer.hostname = thisServer.hostname;
        return true;
      }
  }
  return false;
}

ADDONCREATOR(CPVREon)
