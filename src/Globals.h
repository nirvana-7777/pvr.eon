/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include <string>

static const std::string EON_USER_AGENT = std::string("Kodi/")
    + std::string(STR(KODI_VERSION)) + std::string(" pvr.eon/")
    + std::string(STR(EON_VERSION));

static const std::string GLOBAL_URL = "global.united.cloud/";
static const std::string BROKER_URL = "https://broker." + GLOBAL_URL;
static const std::string PLAYER = "m3u8";
static const std::string SS_DOMAIN = "TBA";
static const std::string SS_USER = "webscuser";
static const std::string SS_SECRET = "k4md93!k334f3";
static const std::string SS_PASS = "xD8iMq1!m94z";
//LG TV
//static const std::string DEVICE_TYPE = "lgw-z81-8jg";
//static const std::string DEVICE_NAME = "LG WEB OS 2020";
//static const std::string DEVICE_MODEL = "OLED65CX9LA";
//static const std::string DEVICE_PLATFORM ="lgw";
//static const std::string SYSTEM_SW = "WebOS";
//static const std::string SYSTEM_VERSION = "04.41.40";

static const std::string CONN_TYPE_WIFI = "WI_FI";
static const std::string CONN_TYPE_ETHERNET= "ETHERNET";
static const std::string CONN_TYPE_MOBILE = "MOBILE";
static const std::string CONN_TYPE_BROWSER = "BROWSER";

struct EonParameter
{
  std::string api_prefix;
  std::string api_selector;
  std::string device_type;
  std::string device_name;
  std::string device_model;
  std::string device_platform;
  std::string device_mac;
  std::string client_sw_version;
  std::string client_sw_build;
  std::string system_sw;
  std::string system_version;
  std::string user_agent;
  std::string client_id;
  std::string client_secret;
};

static const EonParameter EonParameters[2] = {{
                                                //Web Client
                                                "web",
                                                "be",
                                                "web_linux_chrome",
                                                "",
                                                "Chrome 116",
                                                "web",
                                                "",
                                                "",
                                                "",
                                                "Linux",
                                                "x86_64",
                                                "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/116.0.0.0 Safari/537.36",
                                                "b8d9ade4-1093-46a7-a4f7-0e47be463c10",
                                                "1w4dmww87x1e9l89essqvc81pidrqsa0li1rva23"
                                              },
                                              {
                                                //Android TV
                                                "android-tv",
                                                "af31",
                                                "Android 11",
                                                "Android TV 30",
                                                "SHIELD Android TV",
                                                "android_tv",
                                                "",
                                                "8.1.3",
                                                "8.1.35906",
                                                "Android",
                                                "11",
                                                "Mozilla/5.0 (Linux; Android 11; SHIELD Android TV Build/RQ1A.210105.003; wv) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/114.0.5735.196 Mobile Safari/537.36; XDKAndroidWebView/3.0.1/XDKWebView NVIDIA NVIDIA/mdarcy/mdarcy:11/RQ1A.210105.003/7825230_3167.5736:user/release-keys NVIDIA AndroidTV 1.00A_ATV SHIELD Android TV Android/11 ExoPlayer ((1.00A_ATV::1.14.1::androidtv::)",
                                                "5a3e24b8-70cd-4958-b716-af9ce053e594",
                                                "aazy6orsi9elhhs17e47lfb4palgszw6igf4y26z"
                                              }
                                            };
