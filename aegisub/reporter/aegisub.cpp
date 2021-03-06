// Copyright (c) 2009, Amar Takhar <verm@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// $Id$

/// @file aegisub.cpp
/// @brief Aegisub specific configuration options and properties.
/// @ingroup base

#ifndef R_PRECOMP
#endif

#include "aegisub.h"
#include "util.h"

#include <libaegisub/option_value.h>

#ifdef __WINDOWS__
#include "../src/config.h"
#else
#include "../acconf.h"
#endif

Aegisub::Aegisub() {
	std::string default_config("{}");
	opt = new agi::Options(util::config_path() + "config.json", default_config, agi::Options::FLUSH_SKIP);
}


Aegisub::~Aegisub() {
	delete opt;
}


const std::string Aegisub::GetString(std::string key) {
		return opt->Get(key)->GetString();
}


int64_t Aegisub::GetInt(std::string key) {
		return opt->Get(key)->GetInt();
}


bool Aegisub::GetBool(std::string key) {
		return opt->Get(key)->GetBool();
}
