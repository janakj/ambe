/*
 * A library for working with DVSI's AMBE vocoder chips
 *
 * Copyright (C) 2019-2020 Internet Real-Time Lab, Columbia University
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "uri.h"

#include <cstdlib>
#include <iostream>

using namespace std;
using namespace ambe;


URI URI::parse(const string& uri) {
	if (!uri.length())
		throw runtime_error("URI string must not be empty");

	size_t found = uri.find(":");
	if (found == string::npos)
		throw runtime_error("Invalid URI string, expected <scheme>:<authority>");

	auto scheme = uri.substr(0, found);
	auto authority = uri.substr(found + 1, string::npos);

	auto type = scheme;
	for_each(type.begin(), type.end(), [](char& c){ c = ::tolower(c); });

	if      (type == "usb")  return UsbURI(scheme, authority);
	else if (type == "grpc") return GrpcURI(scheme, authority);
	else                     return URI(UriType::UNKNOWN, scheme, authority);
}
