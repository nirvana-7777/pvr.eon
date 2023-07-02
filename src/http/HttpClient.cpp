#include "HttpClient.h"
#include "../Globals.h"
#include <random>
#include <kodi/AddonBase.h>
#include "../Settings.h"
#include "rapidjson/document.h"
#include "../Utils.h"
//#include <botan/base64.h>
#include "../SHA256.h"
#include "../Base64.h"

void HttpClient::SetApi(const std::string& api)
{
  m_api = api;
}

bool HttpClient::RefreshGenericToken()
{
  Curl curl_auth;

  std::string url = BROKER_URL + "oauth/token?grant_type=client_credentials";
  std::string postData = "{}";

  std::string basic_token = CLIENT_ID + ":" + CLIENT_SECRET;
  // Copy input data to a buffer that will be encrypted
//  Botan::secure_vector<uint8_t> bt(basic_token.data(), basic_token.data() + basic_token.length());
//  std::string test1 = base64_encode(basic_token.c_str(), basic_token.length());
//  kodi::Log(ADDON_LOG_DEBUG, "Base64: %s", test1.c_str());
//  curl_auth.AddHeader("Authorization", "Basic " + Botan::base64_encode(bt));
  curl_auth.AddHeader("Authorization", "Basic " + base64_encode(basic_token.c_str(), basic_token.length()));


  int statusCode;
  std::string content_auth = HttpRequestToCurl(curl_auth, "POST", url, postData, statusCode);

  rapidjson::Document doc;
  doc.Parse(content_auth.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to refresh generic access token");
    return false;
  }

  std::string access_token = Utils::JsonStringOrEmpty(doc, "access_token");

  if (!access_token.empty()) {
    m_settings->SetSetting("genericaccesstoken", access_token);
  }

  kodi::Log(ADDON_LOG_DEBUG, "Got Generic Access Token: %s", access_token.c_str());

  return true;
}

bool HttpClient::RefreshSSToken()
{
  Curl curl_auth;

  std::string url = SS_PORTAL + "/gateway/SCAuthAPI/1.0/scauth/auth/authentication"; //TODO: Fix URL
  std::string refresh_token = m_settings->GetSSRefreshToken();
  std::string access_token = m_settings->GetSSAccessToken();
  std::string username = m_settings->GetEonUsername();
  std::string password = m_settings->GetEonPassword();
  std::string postData = "{\"domainId\":\"" + SS_DOMAIN +
                           "\",\"applicationId\":\"vpb\""
                           ",\"grantType\":\"";

  if (!refresh_token.empty()) {
    postData = postData + "refresh\"" +
                          ",\"refreshToken\":\"" + refresh_token + "\"}";
  } else if (!username.empty() && !password.empty()) {
    postData = postData + "password\"" +
                          ",\"password\":\"" + password +  //TODO: Fix Password: Salted AES encrypted hash - Passphrase is SS_PASS
                          "\",\"username\":\"" + username + "\"}";
  } else {
    kodi::Log(ADDON_LOG_ERROR, "Failed to refresh self service token");
    return false;
  }
  std::string basic_token = SS_USER + ":" + SS_SECRET;
  // Copy input data to a buffer that will be encrypted
//  Botan::secure_vector<uint8_t> bt(basic_token.data(), basic_token.data() + basic_token.length());
//  curl_auth.AddHeader("Authorization", "Basic " + Botan::base64_encode(bt));
  curl_auth.AddHeader("Authorization", "Basic " + base64_encode(basic_token.c_str(), basic_token.length()));
  curl_auth.AddHeader("Content-Type", "application/json");

  int statusCode;
  std::string ss_auth = HttpRequestToCurl(curl_auth, "POST", url, postData, statusCode);

  rapidjson::Document doc;
  doc.Parse(ss_auth.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to refresh self service token");
    return false;
  }

  const rapidjson::Value& credentials = doc["UserTokenAuthenticate"];

  std::string ss_identity = Utils::JsonStringOrEmpty(credentials, "identity");
  access_token = Utils::JsonStringOrEmpty(credentials, "accessToken");
  refresh_token = Utils::JsonStringOrEmpty(credentials, "refreshToken");

  if (!refresh_token.empty()) {
    m_settings->SetSetting("ssrefreshtoken", refresh_token);
  }
  if (!access_token.empty()) {
    m_settings->SetSetting("ssaccesstoken", access_token);
  }
  if (!ss_identity.empty()) {
    m_settings->SetSetting("ssidentity", ss_identity);
  }
  kodi::Log(ADDON_LOG_DEBUG, "Got Identity: %s, Access Token: %s, Refresh Token: %s", ss_identity.c_str(), access_token.c_str(), refresh_token.c_str());

  return true;
}

bool HttpClient::RefreshToken()
{
  Curl curl_auth;

  std::string url = m_api + "oauth/token?grant_type=";
  std::string refresh_token = m_settings->GetEonRefreshToken();
  std::string postData = "{}";

  if (!refresh_token.empty()) {
    url += "refresh_token&refresh_token=" + refresh_token;
  }
  else if ((!m_settings->GetEonUsername().empty()) && (!m_settings->GetEonPassword().empty()) && (!m_settings->GetEonDeviceNumber().empty())) {
/*
    auto hash = Botan::HashFunction::create("SHA-256");
    hash->update(m_settings->GetEonUsername());
    std::string user_hash1 = Botan::hex_encode(hash->final());
    kodi::Log(ADDON_LOG_DEBUG, "SHA256 %s", user_hash1.c_str());
*/
    SHA256 sha;
    sha.update(m_settings->GetEonUsername());
    uint8_t * digest = sha.digest();
    std::string user_hash = SHA256::toString(digest);
    std::transform(user_hash.begin(), user_hash.end(), user_hash.begin(), ::toupper);
    kodi::Log(ADDON_LOG_DEBUG, "SHA256 %s", user_hash.c_str());

    delete[] digest;

    std::string password = m_settings->GetEonPassword();
    std::string device_number = m_settings->GetEonDeviceNumber();

    kodi::Log(ADDON_LOG_DEBUG, "Using Device Number %s to login", device_number.c_str());

    std::string boundary = "----WebKitFormBoundary2VHeBtQPpnSo3SjK";
    curl_auth.AddHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

    postData = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"username\"\r\n\r\n" + user_hash + "\r\n--"
                    + boundary + "\r\nContent-Disposition: form-data; name=\"password\"\r\n\r\n" + password + "\r\n--"
                    + boundary + "\r\nContent-Disposition: form-data; name=\"device_number\"\r\n\r\n" + device_number + "\r\n--"
                    + boundary + "--";

    url += "password";
  }
  else {
    kodi::Log(ADDON_LOG_ERROR, "Failed to refresh token");
    return false;
  }

  int statusCode;


  std::string basic_token = CLIENT_ID + ":" + CLIENT_SECRET;
  // Copy input data to a buffer that will be encrypted
//  Botan::secure_vector<uint8_t> bt(basic_token.data(), basic_token.data() + basic_token.length());
//  curl_auth.AddHeader("Authorization", "Basic " + Botan::base64_encode(bt));
  curl_auth.AddHeader("Authorization", "Basic " + base64_encode(basic_token.c_str(), basic_token.length()));

  std::string content_auth = HttpRequestToCurl(curl_auth, "POST", url, postData, statusCode);

  rapidjson::Document doc;
  doc.Parse(content_auth.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to refresh token");
    return false;
  }

  std::string access_token = Utils::JsonStringOrEmpty(doc, "access_token");
  refresh_token = Utils::JsonStringOrEmpty(doc, "refresh_token");
  std::string stream_key = Utils::JsonStringOrEmpty(doc, "stream_key");
  std::string stream_un = Utils::JsonStringOrEmpty(doc, "stream_un");

  m_settings->SetSetting("accesstoken", access_token);
  if (!refresh_token.empty()) {
    m_settings->SetSetting("refreshtoken", refresh_token);
  }
  if (!stream_key.empty()) {
    m_settings->SetSetting("streamkey", stream_key);
  }
  if (!stream_un.empty()) {
    m_settings->SetSetting("streamuser", stream_un);
  }

  return true;
}


HttpClient::HttpClient(CSettings* settings):
  m_settings(settings)
{

}

HttpClient::~HttpClient()
{

}

void HttpClient::ClearSession() {
  m_uuid = GetUUID();
//  m_beakerSessionId = "";
}

std::string HttpClient::GetUUID()
{
  if (!m_uuid.empty())
  {
    return m_uuid;
  }

  m_uuid = GenerateUUID();
//  m_parameterDB->Set("uuid", m_uuid);
  return m_uuid;
}

std::string HttpClient::GenerateUUID()
{
    std::string tmp_s;
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "-";

    srand( (unsigned) time(NULL));

    for (int i = 0; i < 21; ++i)
    {
        tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    return tmp_s;
}
/*
std::string HttpClient::HttpGetCached(const std::string& url, time_t cacheDuration, int &statusCode)
{

  std::string content;
  std::string cacheKey = md5(url);
  statusCode = 200;
  if (!Cache::Read(cacheKey, content))
  {
    content = HttpGet(url, statusCode);
    if (!content.empty())
    {
      time_t validUntil;
      time(&validUntil);
      validUntil += cacheDuration;
      Cache::Write(cacheKey, content, validUntil);
    }
  }
  return content;
}
*/
std::string HttpClient::HttpGet(const std::string& url, int &statusCode)
{
  return HttpRequest("GET", url, "", statusCode);
}

std::string HttpClient::HttpDelete(const std::string& url, int &statusCode)
{
  return HttpRequest("DELETE", url, "", statusCode);
}

std::string HttpClient::HttpPost(const std::string& url, const std::string& postData, int &statusCode)
{
  return HttpRequest("POST", url, postData, statusCode);
}

std::string HttpClient::HttpRequest(const std::string& action, const std::string& url, const std::string& postData, int &statusCode)
{
  Curl curl;
  std::string access_token;

  curl.AddHeader("User-Agent", EON_USER_AGENT);

  size_t found = url.find(SS_PORTAL);
  if (found != std::string::npos) {
    access_token = m_settings->GetSSAccessToken();
    if (!access_token.empty()) {
      curl.AddHeader("accesstoken", access_token);
    }
    std::string basic_token = SS_USER + ":" + SS_SECRET;
    // Copy input data to a buffer that will be encrypted
//    Botan::secure_vector<uint8_t> bt(basic_token.data(), basic_token.data() + basic_token.length());
//    curl.AddHeader("Authorization", "Basic " + Botan::base64_encode(bt));
    curl.AddHeader("Authorization", "Basic " + base64_encode(basic_token.c_str(), basic_token.length()));
  } else {
    if (url.find(BROKER_URL) != std::string::npos || url.find("v1/devices") != std::string::npos) {
      access_token = m_settings->GetGenericAccessToken();
    } else {
      access_token = m_settings->GetEonAccessToken();
    }
    if (!access_token.empty()) {
      curl.AddHeader("Authorization", "bearer " + access_token);
    } else {
      std::string basic_token = CLIENT_ID + ":" + CLIENT_SECRET;
      // Copy input data to a buffer that will be encrypted
//      Botan::secure_vector<uint8_t> bt(basic_token.data(), basic_token.data() + basic_token.length());
//      curl.AddHeader("Authorization", "Basic " + Botan::base64_encode(bt));
      curl.AddHeader("Authorization", "Basic " + base64_encode(basic_token.c_str(), basic_token.length()));
    }
  }
  curl.AddHeader("Content-Type", "application/json");

  found = url.find("/events/epg");
  if (found != std::string::npos) {
    curl.AddHeader("x-ucp-time-format", "timestamp");
  }

  std::string content = HttpRequestToCurl(curl, action, url, postData, statusCode);

  if (statusCode == 401) {
    Curl curl_reauth;
    size_t found = url.find(SS_PORTAL);
    if (found != std::string::npos) {
      if (RefreshSSToken()) {
        access_token = m_settings->GetSSAccessToken();
        curl_reauth.AddHeader("accesstoken", access_token);
        std::string basic_token = SS_USER + ":" + SS_SECRET;
        // Copy input data to a buffer that will be encrypted
//        Botan::secure_vector<uint8_t> bt(basic_token.data(), basic_token.data() + basic_token.length());
//        curl_reauth.AddHeader("Authorization", "Basic " + Botan::base64_encode(bt));
        curl_reauth.AddHeader("Authorization", "Basic " + base64_encode(basic_token.c_str(), basic_token.length()));
      }
    } else {
      bool refresh_successful;
      if (url.find(BROKER_URL) != std::string::npos || url.find("v1/devices") != std::string::npos) {
        refresh_successful = RefreshGenericToken();
        access_token = m_settings->GetGenericAccessToken();
      } else {
        refresh_successful = RefreshToken();
        access_token = m_settings->GetEonAccessToken();
      }
      if (refresh_successful) {
        curl_reauth.AddHeader("Authorization", "bearer " + access_token);
      }
    }
    content = HttpRequestToCurl(curl_reauth, action, url, postData, statusCode);
  }

  if (statusCode >= 400 || statusCode < 200) {
    kodi::Log(ADDON_LOG_ERROR, "Open URL failed with %i.", statusCode);
    if (m_statusCodeHandler != nullptr) {
      m_statusCodeHandler->ErrorStatusCode(statusCode);
    }
    return "";
  }

  return content;
}

std::string HttpClient::HttpRequestToCurl(Curl &curl, const std::string& action,
    const std::string& url, const std::string& postData, int &statusCode)
{
  kodi::Log(ADDON_LOG_DEBUG, "Http-Request: %s %s.", action.c_str(), url.c_str());
  std::string content;
  if (action == "POST")
  {
    content = curl.Post(url, postData, statusCode);
  }
  else if (action == "DELETE")
  {
    content = curl.Delete(url, statusCode);
  }
  else
  {
    content = curl.Get(url, statusCode);
  }
  return content;

}
