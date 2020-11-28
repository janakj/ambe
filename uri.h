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

#pragma once

#include <iostream>
#include "device.h"

using namespace std;

namespace ambe {

	enum class UriType {
		UNKNOWN,
		USB,
		GRPC
	};


	class URI {
	public:
		URI(UriType type, const string& scheme, const string& authority) :
			type(type), scheme(scheme), authority(authority) {
		}

		static URI parse(const string& uri);

		const UriType type;
		const string scheme;
		const string authority;
	};


	class UsbURI : public URI {
	public:
		UsbURI(const string& scheme, const string& authority) :
			URI(UriType::USB, scheme, authority) {
		};
	};


	class GrpcURI : public URI {
	public:
		GrpcURI(const string& scheme, const string& authority) :
			URI(UriType::GRPC, scheme, authority) {
		};
	};
}
