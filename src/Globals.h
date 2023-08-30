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

static const std::string CLIENT_ID_WEB = "b8d9ade4-1093-46a7-a4f7-0e47be463c10";
static const std::string CLIENT_SECRET_WEB = "1w4dmww87x1e9l89essqvc81pidrqsa0li1rva23";

static const std::string CLIENT_ID_ATV = "5a3e24b8-70cd-4958-b716-af9ce053e594";
static const std::string CLIENT_SECRET_ATV = "aazy6orsi9elhhs17e47lfb4palgszw6igf4y26z";

static const std::string SS_PORTAL = "https://mojtelemach.ba";
static const std::string SS_DOMAIN = "TBA";
static const std::string SS_USER = "webscuser";
static const std::string SS_SECRET = "k4md93!k334f3";
static const std::string SS_PASS = "xD8iMq1!m94z";
//Web Client
static const std::string API_PREFIX_WEB = "web";
static const std::string API_SELECTOR_WEB = "be";
static const std::string DEVICE_TYPE_WEB = "web_linux_chrome";
static const std::string DEVICE_NAME_WEB = "";
static const std::string DEVICE_MODEL_WEB = "Chrome 116";
static const std::string DEVICE_PLATFORM_WEB = "web";
static const std::string DEVICE_MAC_WEB = "";
static const std::string CLIENT_SW_VERSION_WEB = "";
static const std::string CLIENT_SW_BUILD_WEB = "";
static const std::string SYSTEM_SW_WEB = "Linux";
static const std::string SYSTEM_VERSION_WEB = "x86_64";
static const std::string USER_AGENT_WEB = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/116.0.0.0 Safari/537.36";
//Android TV
static const std::string API_PREFIX_ATV = "android-tv";
static const std::string API_SELECTOR_ATV = "af31";
static const std::string DEVICE_TYPE_ATV = "Android 11";
static const std::string DEVICE_NAME_ATV = "Android TV 30";
static const std::string DEVICE_MODEL_ATV = "SHIELD Android TV";
static const std::string DEVICE_PLATFORM_ATV = "android_tv";
static const std::string DEVICE_MAC_ATV = "";
static const std::string CLIENT_SW_VERSION_ATV = "8.1.3";
static const std::string CLIENT_SW_BUILD_ATV = "8.1.35906";
static const std::string SYSTEM_SW_ATV = "Android";
static const std::string SYSTEM_VERSION_ATV = "11";
static const std::string USER_AGENT_ATV = "Mozilla/5.0 (Linux; Android 11; SHIELD Android TV Build/RQ1A.210105.003; wv) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/114.0.5735.196 Mobile Safari/537.36; XDKAndroidWebView/3.0.1/XDKWebView NVIDIA NVIDIA/mdarcy/mdarcy:11/RQ1A.210105.003/7825230_3167.5736:user/release-keys NVIDIA AndroidTV 1.00A_ATV SHIELD Android TV Android/11 ExoPlayer ((1.00A_ATV::1.14.1::androidtv::)";
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
