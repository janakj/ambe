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
