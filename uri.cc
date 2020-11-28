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
