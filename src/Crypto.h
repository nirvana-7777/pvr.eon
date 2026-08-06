/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <string>

// Implemented in PVREon.cpp. Declared here so other translation units
// (e.g. StreamRedirectProxy.cpp) can reuse them instead of duplicating
// the AES/url-safe-encoding logic.
std::string urlsafeencode(const std::string& s);
std::string urlsafedecode(const std::string& s);
std::string string_to_hex(const std::string& input);
std::string aes_encrypt_cbc(const std::string& iv_str, const std::string& key, const std::string& plaintext);
