//#ifndef SRC_HTTP_HTTPCLIENT_H_
//#define SRC_HTTP_HTTPCLIENT_H_

#include "Curl.h"
#include "../Settings.h"
//#include "../sql/ParameterDB.h"
#include "HttpStatusCodeHandler.h"

class HttpClient
{
public:
  HttpClient(CSettings* settings);
  ~HttpClient();
//  std::string HttpGetCached(const std::string& url, time_t cacheDuration, int &statusCode);
  std::string HttpGet(const std::string& url, int &statusCode);
  std::string HttpDelete(const std::string& url, int &statusCode);
  std::string HttpPost(const std::string& url, const std::string& postData, int &statusCode);
//  bool GetDeviceFromSerial();
  bool RefreshToken();
  bool RefreshSSToken();
  bool RefreshGenericToken();
  void ClearSession();
  void SetApi(const std::string& api);
  std::string GetUUID();
   void SetStatusCodeHandler(HttpStatusCodeHandler* statusCodeHandler) {
    m_statusCodeHandler = statusCodeHandler;
  }
private:
  std::string HttpRequest(const std::string& action, const std::string& url, const std::string& postData, int &statusCode);
  std::string HttpRequestToCurl(Curl &curl, const std::string& action, const std::string& url, const std::string& postData, int &statusCode);
  std::string GenerateUUID();
  std::string m_uuid;
  std::string m_api;
  std::string client_id;
  std::string client_secret;
  CSettings* m_settings;
  HttpStatusCodeHandler *m_statusCodeHandler = nullptr;
};

//#endif /* SRC_HTTP_HTTPCLIENT_H_ */
