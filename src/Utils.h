#pragma once

#include <sstream>
#include <string>
#include <vector>
#include "rapidjson/document.h"


class Utils
{
public:
  static std::string GetFilePath(const std::string &strPath, bool bUserPath = true);
  static std::string UrlEncode(const std::string &string);
  static double StringToDouble(const std::string &value);
  static int StringToInt(const std::string &value);
  static std::string ReadFile(const std::string& path);
  static std::vector<std::string> SplitString(const std::string &str,
      const char &delim, int maxParts = 0);
  static time_t StringToTime(const std::string &timeString);
  static std::string TimeToString(time_t time);
  static int GetChannelId(const char * strChannelName);
  static std::string GetImageUrl(const std::string& imageToken);
  static std::string JsonStringOrEmpty(const rapidjson::Value& jsonValue, const char* fieldName);
  static int JsonIntOrZero(const rapidjson::Value& jsonValue, const char* fieldName);
  static int64_t JsonInt64OrZero(const rapidjson::Value& jsonValue, const char* fieldName);
  static double JsonDoubleOrZero(const rapidjson::Value& jsonValue, const char* fieldName);
  static bool JsonBoolOrFalse(const rapidjson::Value& jsonValue, const char* fieldName);
  static std::string CreateUUID();
};
