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
