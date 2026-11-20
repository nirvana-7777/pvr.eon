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
#include <kodi/gui/dialogs/OK.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

namespace
{
std::string DescribeSecret(const std::string& value)
{
  return value.empty() ? "empty" : "set(len=" + std::to_string(value.size()) + ")";
}

// Every request needs these regardless of which token ends up on it, so the
// retry that follows a 401 has to set them too -- it builds a fresh Curl and
// used to send only an Authorization header. The EPG endpoint made that
// visible: without x-ucp-time-format it reads fromTime/toTime as formatted
// dates rather than epoch milliseconds and answers HTTP 400 invalid_input,
// so the first tune-in after a token expiry lost its programme window and
// played as a live stream with nothing to rewind into.
void AddRequestHeaders(Curl& curl, const std::string& url)
{
  curl.AddHeader("User-Agent", EON_USER_AGENT);
  curl.AddHeader("Content-Type", "application/json");
  if (url.find("/events/epg") != std::string::npos)
    curl.AddHeader("x-ucp-time-format", "timestamp");
}

std::string PreviewForLog(std::string value)
{
  std::replace(value.begin(), value.end(), '\n', ' ');
  std::replace(value.begin(), value.end(), '\r', ' ');
  if (value.size() > 200)
    value = value.substr(0, 200) + "...";
  return value;
}
} // namespace

// ---------------------------------------------------------------------------
// Vivacom ID sign-in helpers (OAuth2 authorization code + PKCE).
// Everything below mirrors what the eontv.bg web app does in the browser, one
// hop at a time, because Kodi's CURL layer has no cookie jar of its own.
// ---------------------------------------------------------------------------
namespace
{
const char* const kUnreserved =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
const char* const kAlnum =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

std::string FormEncode(const std::string& value)
{
  static const char hex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size() * 3);
  for (unsigned char c : value)
  {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~')
    {
      out.push_back(static_cast<char>(c));
    }
    else
    {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0x0F]);
    }
  }
  return out;
}

int HexVal(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string FormDecode(const std::string& value)
{
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i)
  {
    if (value[i] == '%' && i + 2 < value.size() && HexVal(value[i + 1]) >= 0 &&
        HexVal(value[i + 2]) >= 0)
    {
      out.push_back(static_cast<char>(HexVal(value[i + 1]) * 16 + HexVal(value[i + 2])));
      i += 2;
    }
    else if (value[i] == '+')
    {
      out.push_back(' ');
    }
    else
    {
      out.push_back(value[i]);
    }
  }
  return out;
}

std::vector<std::string> SplitOn(const std::string& str, char delim)
{
  std::vector<std::string> parts;
  size_t start = 0;
  while (true)
  {
    const size_t pos = str.find(delim, start);
    if (pos == std::string::npos)
    {
      parts.emplace_back(str.substr(start));
      break;
    }
    parts.emplace_back(str.substr(start, pos - start));
    start = pos + 1;
  }
  return parts;
}

std::string LowerCase(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(::tolower(c)); });
  return value;
}

bool StartsWith(const std::string& value, const std::string& prefix)
{
  return value.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string& value, const std::string& suffix)
{
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Value of one query parameter, percent-decoded. Empty when absent.
std::string QueryParam(const std::string& url, const std::string& name)
{
  const size_t q = url.find('?');
  if (q == std::string::npos)
    return "";
  size_t end = url.find('#', q);
  const std::string query =
      url.substr(q + 1, end == std::string::npos ? std::string::npos : end - q - 1);
  for (const std::string& pair : SplitOn(query, '&'))
  {
    const size_t eq = pair.find('=');
    if (eq == std::string::npos)
      continue;
    if (pair.substr(0, eq) == name)
      return FormDecode(pair.substr(eq + 1));
  }
  return "";
}

// scheme://host[:port] of an absolute URL.
std::string OriginOf(const std::string& url)
{
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos)
    return "";
  const size_t pathStart = url.find_first_of("/?#", schemeEnd + 3);
  return url.substr(0, pathStart == std::string::npos ? std::string::npos : pathStart);
}

std::string HostOf(const std::string& url)
{
  const std::string origin = OriginOf(url);
  const size_t schemeEnd = origin.find("://");
  if (schemeEnd == std::string::npos)
    return "";
  std::string host = origin.substr(schemeEnd + 3);
  const size_t colon = host.find(':');
  if (colon != std::string::npos)
    host.resize(colon);
  return LowerCase(host);
}

// Scheme + host + path only: query strings carry codes and session keys and
// must never reach the log.
std::string LogUrl(const std::string& url)
{
  const size_t cut = url.find_first_of("?#");
  return cut == std::string::npos ? url : url.substr(0, cut) + "?...";
}

std::string ResolveLocation(const std::string& base, const std::string& location)
{
  if (StartsWith(location, "http://") || StartsWith(location, "https://"))
    return location;
  if (StartsWith(location, "//"))
    return "https:" + location;
  if (StartsWith(location, "/"))
    return OriginOf(base) + location;
  const size_t cut = base.find_first_of("?#");
  const std::string noQuery = cut == std::string::npos ? base : base.substr(0, cut);
  const size_t slash = noQuery.rfind('/');
  return (slash == std::string::npos ? noQuery + "/" : noQuery.substr(0, slash + 1)) + location;
}

std::string RandomString(size_t length, const std::string& charset)
{
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<size_t> dist(0, charset.size() - 1);
  std::string out;
  out.reserve(length);
  for (size_t i = 0; i < length; ++i)
    out.push_back(charset[dist(gen)]);
  return out;
}

// RFC 7636 S256: BASE64URL(SHA256(verifier)) without padding.
std::string PkceChallenge(const std::string& verifier)
{
  SHA256 sha;
  sha.update(verifier);
  uint8_t* digest = sha.digest();
  std::string b64 = base64_encode(reinterpret_cast<const char*>(digest), 32);
  delete[] digest;
  std::replace(b64.begin(), b64.end(), '+', '-');
  std::replace(b64.begin(), b64.end(), '/', '_');
  b64.erase(std::remove(b64.begin(), b64.end(), '='), b64.end());
  return b64;
}

// Minimal per-domain cookie jar. Handles Domain=, Max-Age=0 / empty value /
// 1970 expiry as deletion; Path and Secure are ignored (every hop is https and
// every cookie of interest is Path=/).
class CookieJar
{
public:
  void Store(const std::string& host, const std::vector<std::string>& setCookieLines)
  {
    for (const std::string& line : setCookieLines)
    {
      const std::vector<std::string> attrs = SplitOn(line, ';');
      if (attrs.empty())
        continue;
      const std::string first = Utils::Trim(attrs[0]);
      const size_t eq = first.find('=');
      if (eq == std::string::npos || eq == 0)
        continue;

      Cookie c;
      c.name = first.substr(0, eq);
      c.value = first.substr(eq + 1);
      c.domain = host;
      c.hostOnly = true;
      bool remove = c.value.empty();

      for (size_t i = 1; i < attrs.size(); ++i)
      {
        const std::string attr = Utils::Trim(attrs[i]);
        const size_t aeq = attr.find('=');
        const std::string key = LowerCase(aeq == std::string::npos ? attr : attr.substr(0, aeq));
        const std::string val = aeq == std::string::npos ? "" : attr.substr(aeq + 1);
        if (key == "domain" && !val.empty())
        {
          c.domain = LowerCase(val[0] == '.' ? val.substr(1) : val);
          c.hostOnly = false;
        }
        else if (key == "max-age" && atoi(val.c_str()) <= 0)
        {
          remove = true;
        }
        else if (key == "expires" && val.find("1970") != std::string::npos)
        {
          remove = true;
        }
      }

      m_cookies.erase(std::remove_if(m_cookies.begin(), m_cookies.end(),
                                     [&](const Cookie& e) {
                                       return e.name == c.name && e.domain == c.domain;
                                     }),
                      m_cookies.end());
      if (!remove)
        m_cookies.emplace_back(c);
    }
  }

  std::string HeaderFor(const std::string& host) const
  {
    std::string header;
    for (const Cookie& c : m_cookies)
    {
      const bool match = c.hostOnly ? host == c.domain
                                    : (host == c.domain || EndsWith(host, "." + c.domain));
      if (!match)
        continue;
      if (!header.empty())
        header += "; ";
      header += c.name + "=" + c.value;
    }
    return header;
  }

private:
  struct Cookie
  {
    std::string name;
    std::string value;
    std::string domain;
    bool hostOnly = true;
  };
  std::vector<Cookie> m_cookies;
};

struct HopResult
{
  int status = 0;
  std::string location;
  std::string body;
  bool IsRedirect() const
  {
    return (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) &&
           !location.empty();
  }
};

// One HTTP request with redirects switched off, so the caller sees every 3xx
// and its Location, and with the jar's cookies attached.
HopResult DoHop(CookieJar& jar,
                const std::string& method,
                const std::string& url,
                const std::string& body,
                const std::vector<std::pair<std::string, std::string>>& extraHeaders)
{
  HopResult r;
  Curl curl;
  curl.AddOption("redirect-limit", "0");
  curl.AddHeader("User-Agent", VIVACOM_BROWSER_UA);
  curl.AddHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
  curl.AddHeader("Accept-Language", "bg-BG,bg;q=0.9,en;q=0.8");

  const std::string host = HostOf(url);
  const std::string cookies = jar.HeaderFor(host);
  if (!cookies.empty())
    curl.AddHeader("Cookie", cookies);
  for (const auto& h : extraHeaders)
    curl.AddHeader(h.first, h.second);

  if (method == "POST")
    r.body = curl.Post(url, body, r.status);
  else
    r.body = curl.Get(url, r.status);

  r.location = curl.GetLocation();
  jar.Store(host, curl.GetSetCookieHeaders());
  return r;
}

struct SsoResult
{
  bool ok = false;
  bool badCredentials = false;
  std::string code;
  std::string identityKey;
  std::string state;
  std::string error;
};

// Walks the browser's route: api-web /oauth2/authorize -> (Spring client "idp")
// -> login.vivacom.bg login page -> POST /commonauth -> back through the IdP
// and api-web until the redirect to eontv.bg carries the authorization code.
// The login *page* is never fetched; only its sessionDataKey (found in the
// redirect that leads to it) is needed to submit the credentials.
SsoResult RunVivacomLogin(const std::string& api,
                          const std::string& username,
                          const std::string& password,
                          const std::string& challenge,
                          const std::string& state)
{
  SsoResult res;
  CookieJar jar;

  std::string url = api + "oauth2/authorize?client_id=" + FormEncode(VIVACOM_CLIENT_ID) +
                    "&redirect_uri=" + FormEncode(VIVACOM_REDIRECT_URI) +
                    "&response_type=code&state=" + FormEncode(state) +
                    "&scope=openid&code_challenge=" + FormEncode(challenge) +
                    "&code_challenge_method=S256";
  bool credentialsPosted = false;
  const int kMaxHops = 25;

  for (int hop = 0; hop < kMaxHops; ++hop)
  {
    const HopResult r = DoHop(jar, "GET", url, "", {});
    kodi::Log(ADDON_LOG_DEBUG, "Vivacom sign-in hop %i: GET %s -> %i", hop, LogUrl(url).c_str(),
              r.status);

    if (!r.IsRedirect())
    {
      res.error = "unexpected response (status " + std::to_string(r.status) + ") at " + LogUrl(url);
      return res;
    }

    const std::string next = ResolveLocation(url, r.location);

    // Arrived back at the web app: the code is in the query string.
    if (StartsWith(next, VIVACOM_REDIRECT_URI) &&
        (next.size() == VIVACOM_REDIRECT_URI.size() || next[VIVACOM_REDIRECT_URI.size()] == '?' ||
         next[VIVACOM_REDIRECT_URI.size()] == '/'))
    {
      res.code = QueryParam(next, "code");
      res.identityKey = QueryParam(next, "identity_key");
      res.state = QueryParam(next, "state");
      res.ok = !res.code.empty();
      if (!res.ok)
        res.error = "redirect to the web app carried no authorization code (error=" +
                    QueryParam(next, "error") + ")";
      return res;
    }

    if (next.find("/authenticationendpoint/login.do") != std::string::npos)
    {
      if (credentialsPosted)
      {
        // Sent back to the login form after submitting: Vivacom ID said no.
        res.badCredentials = true;
        res.error = "Vivacom ID sent the browser back to the login form (authFailure=" +
                    QueryParam(next, "authFailure") + ")";
        return res;
      }

      const std::string sessionDataKey = QueryParam(next, "sessionDataKey");
      if (sessionDataKey.empty())
      {
        res.error = "login form redirect had no sessionDataKey";
        return res;
      }

      const std::string loginOrigin = "https://" + VIVACOM_LOGIN_HOST;
      const std::string form = "usernameUserInput=" + FormEncode(username) +
                               "&username=" + FormEncode(username) +
                               "&password=" + FormEncode(password) +
                               "&sessionDataKey=" + FormEncode(sessionDataKey);
      const HopResult post = DoHop(jar, "POST", loginOrigin + "/commonauth", form,
                                   {{"Content-Type", "application/x-www-form-urlencoded"},
                                    {"Origin", loginOrigin},
                                    {"Referer", next}});
      credentialsPosted = true;
      kodi::Log(ADDON_LOG_DEBUG, "Vivacom sign-in hop %i: POST %s/commonauth -> %i", hop,
                loginOrigin.c_str(), post.status);

      if (!post.IsRedirect())
      {
        res.error = "login form submission was not redirected (status " +
                    std::to_string(post.status) + ")";
        return res;
      }
      url = ResolveLocation(loginOrigin + "/commonauth", post.location);
      if (url.find("/authenticationendpoint/login.do") != std::string::npos)
      {
        // Straight back to the login form: Vivacom ID rejected the sign-in.
        res.badCredentials = true;
        res.error = "Vivacom ID sent the browser back to the login form (authFailure=" +
                    QueryParam(url, "authFailure") + ")";
        return res;
      }
      continue;
    }

    url = next;
  }

  res.error = "too many redirects";
  return res;
}
} // namespace

bool HttpClient::IsVivacomSso() const
{
  // Keyed on the brand identifier the provider setting resolves to, never on
  // the setting's numeric position, so reordering or extending the options
  // list cannot silently change which accounts take the SSO route.
  return m_brandIdentifier == "vivacom" && m_settings->GetPlatform() == 0;
}

bool HttpClient::RefreshTokenVivacom()
{
  m_mainTokenHealthy = false;

  const std::string tokenUrl = m_api + "oauth2/token";
  const std::string basicRaw = VIVACOM_CLIENT_ID + ":" + VIVACOM_CLIENT_SECRET;
  const std::string basic = base64_encode(basicRaw.c_str(), static_cast<unsigned int>(basicRaw.length()));

  auto postToken = [&](const std::string& form, int& status) {
    Curl curl;
    curl.AddHeader("User-Agent", VIVACOM_BROWSER_UA);
    curl.AddHeader("Accept", "application/json, text/plain, */*");
    curl.AddHeader("Content-Type", "application/x-www-form-urlencoded");
    curl.AddHeader("Authorization", "Basic " + basic);
    curl.AddHeader("Origin", VIVACOM_REDIRECT_URI);
    curl.AddHeader("Referer", VIVACOM_REDIRECT_URI + "/");
    return HttpRequestToCurl(curl, "POST", tokenUrl, form, status);
  };

  // Parses a token response and stores the session. False if it carried no token.
  auto storeTokens = [&](const std::string& content, int status, const char* flow) {
    rapidjson::Document doc;
    doc.Parse(content.c_str());
    const bool parsed = !doc.GetParseError() && doc.IsObject();
    const std::string access = parsed ? Utils::JsonStringOrEmpty(doc, "access_token") : "";
    const std::string refresh = parsed ? Utils::JsonStringOrEmpty(doc, "refresh_token") : "";
    const std::string streamKey = parsed ? Utils::JsonStringOrEmpty(doc, "stream_key") : "";
    const std::string streamUser = parsed ? Utils::JsonStringOrEmpty(doc, "stream_un") : "";
    const std::string deviceNumber = parsed ? Utils::JsonStringOrEmpty(doc, "device_number") : "";

    if (status != 200 || access.empty())
    {
      kodi::Log(ADDON_LOG_ERROR,
                "Vivacom token request failed. flow=%s status=%i access=%s refresh=%s responseLen=%zu preview=%s",
                flow, status, DescribeSecret(access).c_str(), DescribeSecret(refresh).c_str(),
                content.size(), PreviewForLog(content).c_str());
      return false;
    }

    m_settings->SetSetting("accesstoken", access);
    if (!refresh.empty())
      m_settings->SetSetting("refreshtoken", refresh);
    if (!streamKey.empty())
      m_settings->SetSetting("streamkey", streamKey);
    if (!streamUser.empty())
      m_settings->SetSetting("streamuser", streamUser);
    if (!deviceNumber.empty())
      m_settings->SetSetting("devicenumber", deviceNumber);

    kodi::Log(ADDON_LOG_INFO,
              "Vivacom token request succeeded. flow=%s access=%s refresh=%s streamKey=%s streamUser=%s deviceNumber=%s",
              flow, DescribeSecret(access).c_str(), DescribeSecret(refresh).c_str(),
              DescribeSecret(streamKey).c_str(), DescribeSecret(streamUser).c_str(),
              DescribeSecret(deviceNumber).c_str());
    return true;
  };

  // ---- 1. Silent renewal with the stored refresh token ----
  const std::string storedRefresh = m_settings->GetEonRefreshToken();
  if (!storedRefresh.empty())
  {
    int status = 0;
    const std::string content =
        postToken("grant_type=refresh_token&refresh_token=" + FormEncode(storedRefresh), status);
    if (storeTokens(content, status, "vivacom_refresh_token"))
    {
      m_mainTokenHealthy = true;
      return true;
    }
    if (status < 400 || status >= 500)
    {
      // Network trouble or a server error: keep the refresh token for next time.
      return false;
    }
    kodi::Log(ADDON_LOG_INFO,
              "Stored Vivacom refresh token rejected (status=%i); signing in with the account instead.",
              status);
    m_settings->SetSetting("refreshtoken", "");
  }

  // ---- 2. Full sign-in through Vivacom ID ----
  const std::string username = m_settings->GetEonUsername();
  const std::string password = m_settings->GetEonPassword();
  if (username.empty() || password.empty())
  {
    kodi::gui::dialogs::OK::ShowAndGetInput("PVR EON: " + kodi::addon::GetLocalizedString(30054),
                                            kodi::addon::GetLocalizedString(30055));
    kodi::Log(ADDON_LOG_ERROR, "Vivacom sign-in needs a username and password. username=%s password=%s",
              DescribeSecret(username).c_str(), DescribeSecret(password).c_str());
    return false;
  }

  const std::string verifier = RandomString(64, kUnreserved);
  const std::string state = RandomString(24, kAlnum);
  const SsoResult sso = RunVivacomLogin(m_api, username, password, PkceChallenge(verifier), state);
  if (!sso.ok)
  {
    kodi::Log(ADDON_LOG_ERROR, "Vivacom sign-in failed%s: %s",
              sso.badCredentials ? " (username or password rejected)" : "", sso.error.c_str());
    return false;
  }
  if (!sso.state.empty() && sso.state != state)
    kodi::Log(ADDON_LOG_WARNING, "Vivacom sign-in: returned state differs from the one sent.");
  kodi::Log(ADDON_LOG_DEBUG, "Vivacom sign-in: authorization code=%s identityKey=%s",
            DescribeSecret(sso.code).c_str(), DescribeSecret(sso.identityKey).c_str());

  // The web app sends a stable random id + the identity key as serial_number.
  // Keep the random part so a re-login lands on the same device.
  std::string serial = m_settings->GetEonDeviceSerial();
  const bool serialUsable =
      serial.size() == 35 && serial.find_first_not_of(kAlnum) == std::string::npos;
  if (!serialUsable)
  {
    serial = RandomString(35, kAlnum);
    m_settings->SetSetting("deviceserial", serial);
  }

  const std::string form = "grant_type=authorization_code&client_id=" + FormEncode(VIVACOM_CLIENT_ID) +
                           "&code=" + FormEncode(sso.code) +
                           "&redirect_uri=" + FormEncode(VIVACOM_REDIRECT_URI) +
                           "&code_verifier=" + FormEncode(verifier) +
                           "&serial_number=" + FormEncode(serial + sso.identityKey) +
                           "&device_type=Browser&platform_identifier=web";
  int status = 0;
  const std::string content = postToken(form, status);
  if (!storeTokens(content, status, "vivacom_authorization_code"))
    return false;

  m_mainTokenHealthy = true;
  return true;
}

void HttpClient::SetApi(const std::string& api)
{
  m_api = api;
}

void HttpClient::SetSupportApi(const std::string& supportApi)
{
  m_supportApi = supportApi;
}

bool HttpClient::RefreshGenericToken()
{
  Curl curl_auth;

  std::string url = BROKER_URL + "oauth/token?grant_type=client_credentials";
  std::string postData = "{}";

  std::string basic_token = EonParameters[m_platform].client_id + ":" + EonParameters[m_platform].client_secret;
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
  std::string access_token = Utils::JsonStringOrEmpty(doc, "access_token");
  if (doc.GetParseError() || statusCode != 200 || access_token.empty())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to refresh generic access token. status=%i token=%s responseLen=%zu preview=%s",
              statusCode, DescribeSecret(access_token).c_str(),
              content_auth.size(), PreviewForLog(content_auth).c_str());
    return false;
  }

  if (!access_token.empty()) {
    m_settings->SetSetting("genericaccesstoken", access_token);
  }

  kodi::Log(ADDON_LOG_INFO, "Generic access token refresh succeeded. token=%s",
            DescribeSecret(access_token).c_str());

  return true;
}

bool HttpClient::RefreshSSToken()
{
  Curl curl_auth;

  std::string url = m_supportApi + "/gateway/SCAuthAPI/1.0/scauth/auth/authentication"; //TODO: Fix URL
  std::string refresh_token = m_settings->GetSSRefreshToken();
  std::string access_token = m_settings->GetSSAccessToken();
  std::string username = m_settings->GetEonUsername();
  std::string password = m_settings->GetEonPassword();
  std::string postData = "{\"domainId\":\"" + SS_DOMAIN +
                           "\",\"applicationId\":\"vpb\""
                           ",\"grantType\":\"";
  std::string grant_flow;

  if (!refresh_token.empty()) {
    grant_flow = "refresh";
    postData = postData + "refresh\"" +
                          ",\"refreshToken\":\"" + refresh_token + "\"}";
  } else if (!username.empty() && !password.empty()) {
    grant_flow = "password";
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
  std::string ss_identity;
  if (!doc.GetParseError() && doc.IsObject() && doc.HasMember("UserTokenAuthenticate"))
  {
    const rapidjson::Value& credentials = doc["UserTokenAuthenticate"];
    ss_identity = Utils::JsonStringOrEmpty(credentials, "identity");
    access_token = Utils::JsonStringOrEmpty(credentials, "accessToken");
    refresh_token = Utils::JsonStringOrEmpty(credentials, "refreshToken");
  }
  if (doc.GetParseError() || statusCode != 200 || access_token.empty())
  {
    kodi::Log(ADDON_LOG_ERROR,
              "Failed to refresh self service token. flow=%s status=%i identity=%s access=%s refresh=%s responseLen=%zu preview=%s",
              grant_flow.c_str(), statusCode,
              DescribeSecret(ss_identity).c_str(),
              DescribeSecret(access_token).c_str(),
              DescribeSecret(refresh_token).c_str(),
              ss_auth.size(), PreviewForLog(ss_auth).c_str());
    return false;
  }

  if (!refresh_token.empty()) {
    m_settings->SetSetting("ssrefreshtoken", refresh_token);
  }
  if (!access_token.empty()) {
    m_settings->SetSetting("ssaccesstoken", access_token);
  }
  if (!ss_identity.empty()) {
    m_settings->SetSetting("ssidentity", ss_identity);
  }
  kodi::Log(ADDON_LOG_INFO,
            "Self service token refresh succeeded. flow=%s identity=%s access=%s refresh=%s",
            grant_flow.c_str(),
            DescribeSecret(ss_identity).c_str(),
            DescribeSecret(access_token).c_str(),
            DescribeSecret(refresh_token).c_str());

  return true;
}

bool HttpClient::RefreshToken()
{
  // Pessimistic until this attempt succeeds; every failure path below
  // leaves it false so IsMainTokenHealthy() reflects the latest attempt.
  m_mainTokenHealthy = false;

  // Vivacom accounts live in Vivacom ID, not in EON's own user store, so the
  // password grant below can only ever answer "Bad credentials" for them.
  if (IsVivacomSso())
    return RefreshTokenVivacom();
  if (m_brandIdentifier == "vivacom")
    kodi::Log(ADDON_LOG_WARNING,
              "Vivacom sign-in is only implemented for the Web platform; the legacy "
              "login below is expected to fail for Vivacom accounts.");

  Curl curl_auth;

  std::string url = m_api + "oauth/token?grant_type=";
  std::string refresh_token = m_settings->GetEonRefreshToken();
  std::string postData = "{}";
  std::string grant_flow;
  bool profileRequested = false;

  if (!refresh_token.empty()) {
    grant_flow = "refresh_token";
    url += "refresh_token&refresh_token=" + refresh_token;

    // The web player moves a session onto a viewer profile with this very
    // grant plus one form field; without the field the session stays on the
    // profile it already has.
    if (m_profileId > 0) {
      const std::string boundary = "----WebKitFormBoundary2VHeBtQPpnSo3SjK";
      curl_auth.AddHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
      postData = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"profile_id\"\r\n\r\n"
                      + std::to_string(m_profileId) + "\r\n--" + boundary + "--";
      profileRequested = true;
    }
  }
  else if ((!m_settings->GetEonUsername().empty()) && (!m_settings->GetEonPassword().empty()) && (!m_settings->GetEonDeviceNumber().empty()) && (m_settings->GetPlatform() != 1)) {
    grant_flow = "password_device_number";
    SHA256 sha;
    sha.update(m_settings->GetEonUsername());
    uint8_t * digest = sha.digest();
    std::string user_hash = SHA256::toString(digest);
    std::transform(user_hash.begin(), user_hash.end(), user_hash.begin(), ::toupper);

    delete[] digest;

    std::string password = m_settings->GetEonPassword();
    std::string device_number = m_settings->GetEonDeviceNumber();

    kodi::Log(ADDON_LOG_DEBUG,
              "Refreshing main token using password flow. deviceNumber=%s username=%s",
              DescribeSecret(device_number).c_str(),
              DescribeSecret(m_settings->GetEonUsername()).c_str());

    std::string boundary = "----WebKitFormBoundary2VHeBtQPpnSo3SjK";
    curl_auth.AddHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

    postData = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"username\"\r\n\r\n" + user_hash + "\r\n--"
                    + boundary + "\r\nContent-Disposition: form-data; name=\"password\"\r\n\r\n" + password + "\r\n--"
                    + boundary + "\r\nContent-Disposition: form-data; name=\"device_number\"\r\n\r\n" + device_number + "\r\n--"
                    + boundary + "--";

    url += "password";
  }
  else {
    //Try to login with OTP
    std::string generic_access_token = m_settings->GetGenericAccessToken();
    if ((m_settings->GetPlatform() == 1) && (!generic_access_token.empty()) && (!m_settings->GetEonDeviceNumber().empty())) {
      grant_flow = "otp";
      Curl curl_otp;
      int statusCode_otp;
      std::string url_otp = m_api + "v1/otp?deviceNumber=" + m_settings->GetEonDeviceNumber();
      curl_otp.AddHeader("User-Agent", EON_USER_AGENT);
      curl_otp.AddHeader("Authorization", "bearer " + generic_access_token);
      std::string otp_response = HttpRequestToCurl(curl_otp, "GET", url_otp, "", statusCode_otp);
      rapidjson::Document doc;
      doc.Parse(otp_response.c_str());
      if (doc.GetParseError())
      {
        kodi::Log(ADDON_LOG_ERROR, "Failed to get OTP. status=%i responseLen=%zu preview=%s",
                  statusCode_otp, otp_response.size(), PreviewForLog(otp_response).c_str());
        return false;
      }
      std::string otp = Utils::JsonStringOrEmpty(doc, "otp");
      int expires = (int) Utils::JsonIntOrZero(doc, "expiresInSeconds") / 60;
      std::string text = "\n" + otp + "\n\n" + kodi::addon::GetLocalizedString(30047) + std::to_string(expires) + kodi::addon::GetLocalizedString(30048) + "\n";
      kodi::gui::dialogs::OK::ShowAndGetInput(kodi::addon::GetLocalizedString(30046), text);
      url += "otp&otp=" + otp + "&device_number=" + m_settings->GetEonDeviceNumber();
    } else {
      kodi::gui::dialogs::OK::ShowAndGetInput("PVR EON: " + kodi::addon::GetLocalizedString(30054), kodi::addon::GetLocalizedString(30055));
      kodi::Log(ADDON_LOG_ERROR,
                "Failed to refresh token. platform=%i username=%s password=%s refresh=%s generic=%s deviceNumber=%s",
                m_settings->GetPlatform(),
                DescribeSecret(m_settings->GetEonUsername()).c_str(),
                DescribeSecret(m_settings->GetEonPassword()).c_str(),
                DescribeSecret(m_settings->GetEonRefreshToken()).c_str(),
                DescribeSecret(m_settings->GetGenericAccessToken()).c_str(),
                DescribeSecret(m_settings->GetEonDeviceNumber()).c_str());
      return false;
    }
  }

  int statusCode;

  std::string basic_token = EonParameters[m_platform].client_id + ":" + EonParameters[m_platform].client_secret;
  curl_auth.AddHeader("Authorization", "Basic " + base64_encode(basic_token.c_str(), basic_token.length()));

  std::string content_auth = HttpRequestToCurl(curl_auth, "POST", url, postData, statusCode);

  rapidjson::Document doc;
  doc.Parse(content_auth.c_str());
  std::string access_token = Utils::JsonStringOrEmpty(doc, "access_token");
  refresh_token = Utils::JsonStringOrEmpty(doc, "refresh_token");
  std::string stream_key = Utils::JsonStringOrEmpty(doc, "stream_key");
  std::string stream_un = Utils::JsonStringOrEmpty(doc, "stream_un");
  // NEW: the login/token exchange is the only place that hands us the
  // deviceNumber the CDN expects as device= in the /stream URL. Persist it.
  std::string device_number = Utils::JsonStringOrEmpty(doc, "device_number");

  if (doc.GetParseError() || statusCode != 200 || access_token.empty())
  {
    kodi::Log(ADDON_LOG_ERROR,
              "Failed to refresh token. flow=%s status=%i access=%s refresh=%s streamKey=%s streamUser=%s deviceNumber=%s responseLen=%zu preview=%s",
              grant_flow.c_str(),
              statusCode,
              DescribeSecret(access_token).c_str(),
              DescribeSecret(refresh_token).c_str(),
              DescribeSecret(stream_key).c_str(),
              DescribeSecret(stream_un).c_str(),
              DescribeSecret(device_number).c_str(),
              content_auth.size(),
              PreviewForLog(content_auth).c_str());
    // A refusal of a refresh that asked for a profile says nothing certain
    // about the refresh token itself (the profile may be gone or need a
    // PIN), so it must not cost the stored session: stop asking for the
    // profile and refresh plainly instead.
    if (grant_flow == "refresh_token" && profileRequested && statusCode >= 400 && statusCode < 500)
    {
      kodi::Log(ADDON_LOG_INFO,
                "Profile id=%i was refused by the server (status=%i); keeping the current profile.",
                m_profileId, statusCode);
      m_profileId = 0;
      return RefreshToken();
    }
    // NEW: only a definitive rejection (4xx) of the refresh_token grant
    // means the stored refresh token is dead -- drop it and fall back to a
    // full login once. A network error or 5xx keeps the token untouched so
    // a transient failure can no longer destroy a restorable session.
    if (grant_flow == "refresh_token" && statusCode >= 400 && statusCode < 500)
    {
      kodi::Log(ADDON_LOG_INFO,
                "Stored refresh token rejected by the server (status=%i); retrying with a fresh login.",
                statusCode);
      m_settings->SetSetting("refreshtoken", "");
      return RefreshToken();
    }
    return false;
  }

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
  // NEW: without this, device= in every CDN URL is empty and the edge 403s.
  if (!device_number.empty()) {
    m_settings->SetSetting("devicenumber", device_number);
  }

  kodi::Log(ADDON_LOG_INFO,
            "Main token refresh succeeded. flow=%s access=%s refresh=%s streamKey=%s streamUser=%s deviceNumber=%s",
            grant_flow.c_str(),
            DescribeSecret(access_token).c_str(),
            DescribeSecret(refresh_token).c_str(),
            DescribeSecret(stream_key).c_str(),
            DescribeSecret(stream_un).c_str(),
            DescribeSecret(device_number).c_str());

  // Remembered across restarts, so the next start knows which profile the
  // stored session is on without spending a token exchange to find out.
  // Empty stands for the account's default profile.
  m_activeProfileId = Utils::JsonIntOrZero(doc, "active_profile_id");
  const bool onDefaultProfile = Utils::JsonBoolOrFalse(doc, "active_profile_is_default");
  m_settings->SetSetting("activeprofile", (m_activeProfileId > 0 && !onDefaultProfile)
                                              ? std::to_string(m_activeProfileId)
                                              : "");
  m_mainTokenHealthy = true;

  if (m_profileId > 0 && m_activeProfileId != m_profileId)
  {
    // A fresh login always lands on the account's default profile; now that
    // there is a refresh token, one more exchange moves it over.
    if (grant_flow != "refresh_token" && !refresh_token.empty())
      return RefreshToken();

    kodi::Log(ADDON_LOG_INFO,
              "Asked for profile id=%i but the token is bound to id=%i; keeping that profile.",
              m_profileId, m_activeProfileId);
    m_profileId = 0;
  }

  return true;
}

bool HttpClient::SwitchProfile(int profileId)
{
  std::lock_guard<std::mutex> lock(m_authMutex);

  // Vivacom ID sessions are renewed through its own identity provider,
  // which this exchange is not part of.
  if (IsVivacomSso() || profileId <= 0)
    return false;

  m_profileId = profileId;
  return RefreshToken() && m_activeProfileId == profileId;
}

HttpClient::HttpClient(CSettings* settings):
  m_settings(settings)
{
  m_platform = m_settings->GetPlatform();
  /*
  if (m_settings->GetPlatform() == 1) {
    client_id = CLIENT_ID_ATV;
    client_secret = CLIENT_SECRET_ATV;
  } else {
    client_id = CLIENT_ID_WEB;
    client_secret = CLIENT_SECRET_WEB;
  }
  */
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
  std::lock_guard<std::mutex> lock(m_authMutex);
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

// NEW: PATCH, used by the v2/devices endpoint.
std::string HttpClient::HttpPatch(const std::string& url, const std::string& postData, int &statusCode)
{
  return HttpRequest("PATCH", url, postData, statusCode);
}

bool HttpClient::UsesGenericToken(const std::string& url) const
{
  // v2/devices authenticates exactly like v1/devices. Vivacom is the
  // exception: its web app registers the device with the signed-in user's
  // own token, not the broker's client-credentials token.
  const bool isDeviceEndpoint = (url.find("/v2/devices") != std::string::npos ||
                                 url.find("/v1/devices") != std::string::npos) &&
                                !IsVivacomSso();
  return url.find(BROKER_URL) != std::string::npos || isDeviceEndpoint;
}

std::string HttpClient::ApplyAuthHeadersLocked(Curl& curl, const std::string& url,
                                               std::string& authMode)
{
  std::string access_token;
  authMode.clear();

  if (url.find(m_supportApi) != std::string::npos) {
    access_token = m_settings->GetSSAccessToken();
    if (!access_token.empty()) {
      curl.AddHeader("accesstoken", access_token);
      authMode = "ss-access-token";
    }
    std::string basic_token = SS_USER + ":" + SS_SECRET;
    curl.AddHeader("Authorization", "Basic " + base64_encode(basic_token.c_str(), basic_token.length()));
    if (authMode.empty())
      authMode = "ss-basic";
    return access_token;
  }

  if (UsesGenericToken(url)) {
    access_token = m_settings->GetGenericAccessToken();
    authMode = access_token.empty() ? "generic-basic" : "generic-bearer";
  } else {
    access_token = m_settings->GetEonAccessToken();
    authMode = access_token.empty() ? "main-basic" : "main-bearer";
  }

  if (!access_token.empty()) {
    curl.AddHeader("Authorization", "bearer " + access_token);
  } else {
    std::string basic_token = EonParameters[m_platform].client_id + ":" + EonParameters[m_platform].client_secret;
    curl.AddHeader("Authorization", "Basic " + base64_encode(basic_token.c_str(), basic_token.length()));
  }

  return access_token;
}

bool HttpClient::RefreshTokenForUrlLocked(const std::string& url)
{
  if (url.find(m_supportApi) != std::string::npos)
    return RefreshSSToken();
  if (UsesGenericToken(url))
    return RefreshGenericToken();
  return RefreshToken();
}

std::string HttpClient::HttpRequest(const std::string& action, const std::string& url, const std::string& postData, int &statusCode)
{
  Curl curl;
  std::string access_token;
  std::string auth_mode;

  AddRequestHeaders(curl, url);
  {
    std::lock_guard<std::mutex> lock(m_authMutex);
    access_token = ApplyAuthHeadersLocked(curl, url, auth_mode);
  }

  std::string content = HttpRequestToCurl(curl, action, url, postData, statusCode);

  if (statusCode == 401) {
    kodi::Log(ADDON_LOG_INFO,
              "HTTP 401 for %s %s. auth=%s payloadLen=%zu, attempting token refresh.",
              action.c_str(), url.c_str(), auth_mode.c_str(), postData.size());

    std::lock_guard<std::mutex> lock(m_authMutex);

    Curl curl_reauth;
    AddRequestHeaders(curl_reauth, url);
    std::string retry_auth_mode;
    std::string current_token = ApplyAuthHeadersLocked(curl_reauth, url, retry_auth_mode);

    // With several requests in flight, another thread may already have
    // refreshed the token this request was signed with while we waited for
    // the lock. Retrying with the current token first keeps a burst of 401s
    // from turning into a burst of refreshes that invalidate each other.
    bool refresh_successful = true;
    if (current_token.empty() || current_token == access_token) {
      refresh_successful = RefreshTokenForUrlLocked(url);
      if (refresh_successful)
        current_token = ApplyAuthHeadersLocked(curl_reauth, url, retry_auth_mode);
    } else {
      kodi::Log(ADDON_LOG_INFO,
                "Token was already refreshed by another request; retrying without refreshing again.");
    }

    if (refresh_successful) {
      content = HttpRequestToCurl(curl_reauth, action, url, postData, statusCode);
      kodi::Log(ADDON_LOG_INFO, "HTTP retry after refresh completed. auth=%s status=%i responseLen=%zu",
                retry_auth_mode.c_str(), statusCode, content.size());
    } else {
      std::string refresh_token = m_settings->GetEonRefreshToken();
      if (!refresh_token.empty() && url.find(m_supportApi) == std::string::npos &&
          !UsesGenericToken(url)) {
        // Try to get a new token as last resort. NOTE: the stored refresh
        // token is intentionally no longer cleared here. RefreshToken()
        // itself drops it when the server definitively rejects it, so a
        // transient failure cannot wipe a restorable session anymore.
        refresh_successful = RefreshToken();
        ApplyAuthHeadersLocked(curl_reauth, url, retry_auth_mode);
        if (refresh_successful) {
          content = HttpRequestToCurl(curl_reauth, action, url, postData, statusCode);
          kodi::Log(ADDON_LOG_INFO, "HTTP retry after last-resort refresh completed. status=%i responseLen=%zu",
                    statusCode, content.size());
        }
      }
    }
  }

  if (statusCode >= 400 || statusCode < 200) {
    kodi::Log(ADDON_LOG_ERROR, "Open URL failed with %i. method=%s url=%s auth=%s responseLen=%zu preview=%s",
              statusCode, action.c_str(), url.c_str(), auth_mode.c_str(), content.size(),
              PreviewForLog(content).c_str());
    if (m_statusCodeHandler != nullptr) {
      m_statusCodeHandler->ErrorStatusCode(statusCode);
    }
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
  // NEW: PATCH branch.
  else if (action == "PATCH")
  {
    content = curl.Patch(url, postData, statusCode);
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