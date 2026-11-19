//#ifndef SRC_HTTP_HTTPCLIENT_H_
//#define SRC_HTTP_HTTPCLIENT_H_

#include <mutex>

#include "Curl.h"
#include "../Settings.h"
//#include "../sql/ParameterDB.h"
#include "HttpStatusCodeHandler.h"

// Internal to the add-on's own library, like CSettings, which it holds a
// pointer to: a more visible class than its member's type makes GCC warn.
class ATTR_DLL_LOCAL HttpClient
{
public:
  HttpClient(CSettings* settings);
  ~HttpClient();
  //  std::string HttpGetCached(const std::string& url, time_t cacheDuration, int &statusCode);
  std::string HttpGet(const std::string& url, int &statusCode);
  std::string HttpDelete(const std::string& url, int &statusCode);
  std::string HttpPost(const std::string& url, const std::string& postData, int &statusCode);
  // NEW: v2/devices uses PATCH.
  std::string HttpPatch(const std::string& url, const std::string& postData, int &statusCode);
  //  bool GetDeviceFromSerial();
  bool RefreshToken();
  bool RefreshSSToken();
  bool RefreshGenericToken();
  void ClearSession();
  void SetApi(const std::string& api);
  // Which brand the provider setting stands for, by the broker's stable
  // identifier (e.g. "vivacom"), so login-mechanism decisions never depend
  // on the options list's numeric order.
  void SetBrandIdentifier(const std::string& brandIdentifier) {
    m_brandIdentifier = brandIdentifier;
  }
  void SetSupportApi(const std::string& supportApi);
  std::string GetUUID();
  // NEW: false while the most recent main-token refresh attempt failed.
  // Lets callers avoid destructive recovery (device re-registration) when
  // the real problem is that the account cannot obtain tokens at all.
  bool IsMainTokenHealthy() const { return m_mainTokenHealthy; }
  // True when the configured provider is Vivacom on the web platform: its
  // accounts live in Vivacom ID and need the browser-style SSO login.
  bool IsVivacomSso() const;
  // Moves the signed-in session onto one of the account's viewer profiles,
  // and keeps it there across later token refreshes. True when the token
  // is bound to that profile afterwards.
  bool SwitchProfile(int profileId);
  void SetStatusCodeHandler(HttpStatusCodeHandler* statusCodeHandler) {
    m_statusCodeHandler = statusCodeHandler;
  }
private:
  bool RefreshTokenVivacom();
  // True for URLs that authenticate with the broker's client-credentials
  // token rather than the signed-in user's token. Vivacom is the exception
  // on the devices endpoints: its web app registers the device with the
  // user's own token.
  bool UsesGenericToken(const std::string& url) const;
  // Applies the auth header(s) the URL calls for and returns the token used
  // (empty when it fell back to Basic). Caller must hold m_authMutex.
  std::string ApplyAuthHeadersLocked(Curl& curl, const std::string& url, std::string& authMode);
  // Refreshes whichever token the URL authenticates with. Caller must hold
  // m_authMutex.
  bool RefreshTokenForUrlLocked(const std::string& url);
  std::string HttpRequest(const std::string& action, const std::string& url, const std::string& postData, int &statusCode);
  std::string HttpRequestToCurl(Curl &curl, const std::string& action, const std::string& url, const std::string& postData, int &statusCode);
  std::string GenerateUUID();
  std::string m_uuid;
  std::string m_api;
  std::string m_supportApi;
  int m_platform;
  /*
  std::string client_id;
  std::string client_secret;
  */
  CSettings* m_settings;
  // Serializes token reads/refreshes and the lazily generated UUID across
  // concurrent requests: Kodi calls the PVR API from several threads, and a
  // burst of 401s must not turn into a burst of refreshes that invalidate
  // each other.
  std::mutex m_authMutex;
  std::string m_brandIdentifier;
  bool m_mainTokenHealthy = true;
  // Profile asked for with every refresh_token grant; 0 leaves the choice
  // to the backend (the account's default profile on a fresh login).
  int m_profileId = 0;
  int m_activeProfileId = 0;
  HttpStatusCodeHandler *m_statusCodeHandler = nullptr;
};

//#endif /* SRC_HTTP_HTTPCLIENT_H_ */