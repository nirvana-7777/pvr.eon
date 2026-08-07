#include "Utils.h"
#ifdef TARGET_WINDOWS
#include "windows.h"
#endif

#include <algorithm>
#include <chrono>
#include <iterator>
#include <cerrno>
#include <cstdlib>

#include <iostream>
#include <kodi/Filesystem.h>
#include <kodi/General.h>

std::string Utils::GetFilePath(const std::string &strPath, bool bUserPath)
{
  return bUserPath ? kodi::addon::GetUserPath(strPath) : kodi::addon::GetAddonPath(strPath);
}

// http://stackoverflow.com/a/17708801
std::string Utils::UrlEncode(const std::string &value)
{
  static const char hex_digits[] = "0123456789abcdef";
  std::string escaped;
  escaped.reserve(value.size() * 3);

  for (char c : value) {
      // Keep alphanumeric and other accepted characters intact
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '!') // Exclamation mark should not be here but Zattoo does not correctly encode it
    {
      escaped.push_back(c);
      continue;
    }

    // Any other characters are percent-encoded
    escaped.push_back('%');
    escaped.push_back(hex_digits[(static_cast<unsigned char>(c) >> 4) & 0x0F]);
    escaped.push_back(hex_digits[static_cast<unsigned char>(c) & 0x0F]);
  }

  return escaped;
}

double Utils::StringToDouble(const std::string &value)
{
  errno = 0;
  char* end = nullptr;
  const double result = std::strtod(value.c_str(), &end);
  if (end == value.c_str() || errno != 0)
    return 0.0;
  return result;
}

int Utils::StringToInt(const std::string &value)
{
  return (int) StringToDouble(value);
}

std::vector<std::string> Utils::SplitString(const std::string &str,
    const char &delim, int maxParts)
{
  typedef std::string::const_iterator iter;
  iter beg = str.begin();
  std::vector < std::string > tokens;

  while (beg != str.end())
  {
    if (maxParts == 1)
    {
      tokens.emplace_back(beg, str.end());
      break;
    }
    maxParts--;
    iter temp = find(beg, str.end(), delim);
    if (beg != str.end())
      tokens.emplace_back(beg, temp);
    beg = temp;
    while ((beg != str.end()) && (*beg == delim))
      beg++;
  }

  return tokens;
}

std::string Utils::ReadFile(const std::string& path)
{
  kodi::vfs::CFile file;
  if (!file.CURLCreate(path) || !file.CURLOpen(0))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to open file [%s].", path.c_str());
    return "";
  }

  char buf[1025];
  ssize_t nbRead;
  std::string content;
  while ((nbRead = file.Read(buf, 1024)) > 0)
  {
    buf[nbRead] = 0;
    content.append(buf);
  }

  return content;

}

time_t Utils::StringToTime(const std::string &timeString)
{
  struct tm tm{};

  int year, month, day, h, m, s, tzh, tzm;
  if (sscanf(timeString.c_str(), "%d-%d-%dT%d:%d:%d%d", &year, &month, &day, &h,
      &m, &s, &tzh) < 7)
  {
    tzh = 0;
  }
  tzm = tzh % 100;
  tzh = tzh / 100;

  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = h - tzh;
  tm.tm_min = m - tzm;
  tm.tm_sec = s;

  time_t ret = timegm(&tm);
  return ret;
}

std::string Utils::TimeToString(const time_t time)
{
  char time_str[21] = "";
  std::tm* pstm = std::localtime(&time);
  // 2019-01-20T23:59:59
  std::strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", pstm);
  return time_str;
}

int Utils::GetChannelId(const char * strChannelName)
{
  int iId = 0;
  int c;
  while ((c = *strChannelName++))
    iId = ((iId << 5) + iId) + c; /* iId * 33 + c */
  return abs(iId);
}

std::string Utils::GetImageUrl(const std::string& imageToken) {
  return "https://images.zattic.com/cms/" + imageToken + "/format_640x360.jpg";
}

std::string Utils::JsonStringOrEmpty(const rapidjson::Value& jsonValue, const char* fieldName)
{
  if (!jsonValue.HasMember(fieldName) || !jsonValue[fieldName].IsString())
  {
    return "";
  }
  return jsonValue[fieldName].GetString();
}

double Utils::JsonDoubleOrZero(const rapidjson::Value& jsonValue, const char* fieldName)
{
  if (!jsonValue.HasMember(fieldName) || !jsonValue[fieldName].IsDouble())
  {
    return 0;
  }
  return jsonValue[fieldName].GetDouble();
}

int64_t Utils::JsonInt64OrZero(const rapidjson::Value& jsonValue, const char* fieldName)
{
  if (!jsonValue.HasMember(fieldName) || !jsonValue[fieldName].IsInt64())
  {
    return 0;
  }
  return jsonValue[fieldName].GetInt64();
}

int Utils::JsonIntOrZero(const rapidjson::Value& jsonValue, const char* fieldName)
{
  if (!jsonValue.HasMember(fieldName) || !jsonValue[fieldName].IsInt())
  {
    return 0;
  }
  return jsonValue[fieldName].GetInt();
}

bool Utils::JsonBoolOrFalse(const rapidjson::Value& jsonValue, const char* fieldName)
{
  if (!jsonValue.HasMember(fieldName))
  {
    return false;
  }

  if (jsonValue[fieldName].IsBool())
  {
    return jsonValue[fieldName].GetBool();
  }

  if (jsonValue[fieldName].IsInt())
  {
    return jsonValue[fieldName].GetInt() != 0;
  }

  return false;
}

std::string Utils::CreateUUID()
{
  // taken from pvr.dvblink
  using namespace std::chrono;

  std::string uuid;
  int64_t seed_value =
      duration_cast<milliseconds>(
          time_point_cast<milliseconds>(high_resolution_clock::now()).time_since_epoch())
          .count();
  seed_value = seed_value % 1000000000;
  srand((unsigned int)seed_value);

  //fill in uuid string from a template
  std::string template_str = "xxxxxxxx-xxxx-4xxx-8xxx-xxxxxxxxxxxx";
  for (size_t i = 0; i < template_str.size(); i++)
  {
    if (template_str[i] == 'x')
    {
      double a1 = rand();
      double a3 = RAND_MAX;
      unsigned char ch = (unsigned char)(a1 * 15 / a3);
      char buf[8];
      sprintf(buf, "%x", ch);
      uuid += buf;
    }
    else
    {
      uuid += template_str[i];
    }
  }
  return uuid;
}

bool Utils::CheckInputstreamInstalledAndEnabled(const std::string& inputstreamName)
{
  std::string version;
  bool enabled;

  if (kodi::IsAddonAvailable(inputstreamName, version, enabled))
  {
    if (!enabled)
    {
      std::string message = kodi::tools::StringUtils::Format(kodi::addon::GetLocalizedString(30502).c_str(), inputstreamName.c_str());
      kodi::QueueNotification(QueueMsg::QUEUE_ERROR, kodi::addon::GetLocalizedString(30500), message);
      return false;
    }
  }
  else // Not installed
  {
    std::string message = kodi::tools::StringUtils::Format(kodi::addon::GetLocalizedString(30501).c_str(), inputstreamName.c_str());
    kodi::QueueNotification(QueueMsg::QUEUE_ERROR, kodi::addon::GetLocalizedString(30500), message);
    return false;
  }

  return true;
}

std::string Utils::Trim(const std::string& value)
{
  size_t end = value.size();
  while (end > 0 && (value[end - 1] == '\r' || value[end - 1] == '\n' ||
                      value[end - 1] == ' ' || value[end - 1] == '\t'))
    --end;

  size_t start = 0;
  while (start < end && (value[start] == ' ' || value[start] == '\t'))
    ++start;

  return value.substr(start, end - start);
}

std::string Utils::ResolvePlaylistUrl(const std::string& baseUrl, const std::string& childUrl)
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

namespace
{
long long BandwidthFromStreamInfLine(const std::string& line)
{
  const std::string key = "BANDWIDTH=";
  const size_t keyPos = line.find(key);
  if (keyPos == std::string::npos)
    return -1;

  size_t digitsStart = keyPos + key.size();
  size_t digitsEnd = digitsStart;
  while (digitsEnd < line.size() && isdigit(static_cast<unsigned char>(line[digitsEnd])))
    ++digitsEnd;

  if (digitsEnd == digitsStart)
    return -1;

  try
  {
    return std::stoll(line.substr(digitsStart, digitsEnd - digitsStart));
  }
  catch (...)
  {
    return -1;
  }
}
} // namespace

std::string Utils::SelectVariantPlaylistUrl(const std::string& manifestBody,
                                             const std::string& baseUrl,
                                             int preference)
{
  if (preference != 1 && preference != 2)
    return "";

  long long bestBandwidth = -1;
  std::string bestUri;

  size_t pos = 0;
  while (pos < manifestBody.size())
  {
    const size_t lineEnd = manifestBody.find('\n', pos);
    const std::string line = Trim(manifestBody.substr(pos, lineEnd == std::string::npos ? std::string::npos : lineEnd - pos));
    pos = lineEnd == std::string::npos ? manifestBody.size() : lineEnd + 1;

    if (line.rfind("#EXT-X-STREAM-INF", 0) != 0)
      continue;

    const long long bandwidth = BandwidthFromStreamInfLine(line);

    std::string uri;
    while (pos < manifestBody.size())
    {
      const size_t uriEnd = manifestBody.find('\n', pos);
      uri = Trim(manifestBody.substr(pos, uriEnd == std::string::npos ? std::string::npos : uriEnd - pos));
      pos = uriEnd == std::string::npos ? manifestBody.size() : uriEnd + 1;
      if (uri.empty() || uri[0] == '#')
      {
        uri.clear();
        continue;
      }
      break;
    }

    if (uri.empty() || bandwidth < 0)
      continue;

    const bool isBetter = bestBandwidth < 0 ||
                           (preference == 1 && bandwidth > bestBandwidth) ||
                           (preference == 2 && bandwidth < bestBandwidth);
    if (isBetter)
    {
      bestBandwidth = bandwidth;
      bestUri = uri;
    }
  }

  if (bestUri.empty())
    return "";

  return ResolvePlaylistUrl(baseUrl, bestUri);
}
