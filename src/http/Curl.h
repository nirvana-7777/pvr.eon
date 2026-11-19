#include <string>
#include <map>
#include <vector>

class Curl
{
public:
  Curl();
  ~Curl();
  std::string Delete(const std::string& url, int &statusCode);
  std::string Get(const std::string& url, int &statusCode);
  std::string Post(const std::string& url, const std::string& postData,
      int &statusCode);
  std::string Patch(const std::string& url, const std::string& postData,
      int &statusCode);
  void AddHeader(const std::string& name, const std::string& value);
  void AddOption(const std::string& name, const std::string& value);
  void ResetHeaders();
  std::string GetCookie(const std::string& name);
  std::string GetLocation() {
    return m_location;
  }
  // Raw Set-Cookie header lines of the last response (attributes included),
  // for callers that run their own cookie jar across redirect hops.
  const std::vector<std::string>& GetSetCookieHeaders() const {
    return m_setCookieHeaders;
  }

private:
  std::string Request(const std::string& action, const std::string& url,
                              const std::string& postData, int &statusCode);
  std::string Base64Encode(unsigned char const* in, unsigned int in_len,
      bool urlEncode);
  std::map<std::string, std::string> m_headers;
  std::map<std::string, std::string> m_options;
  std::map<std::string, std::string> m_cookies;
  std::string m_location;
  std::vector<std::string> m_setCookieHeaders;
};
