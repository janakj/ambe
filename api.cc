#include "api.h"

#include <termios.h>
#include <stdio.h>
#include <iostream>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <assert.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <stdint.h>
#include <iomanip>

using namespace std;
using namespace std::placeholders;
using namespace ambe;

namespace ambe {

	void swap(int16_t* dst, const int16_t* src, size_t count) {
		for(uint i = 0; i < count; i++) dst[i] = bswap_16(src[i]);
	}


	template<typename T, typename... Args>
	static T* parse(const Packet& packet, PacketType type, Args ...args) {
		if (packet.type() != type)
			throw runtime_error("Invalid packet type");

		auto payload = packet.payload<T>();
		if (!payload->valid(forward<Args>(args)...))
			throw runtime_error("Invalid response received");

		return payload;
	}

	template<typename T, typename... Args>
	static T* parse(const Packet& packet, PacketType type, uint8_t channel, Args ...args) {
		if (packet.type() != type)
			throw runtime_error("Invalid packet type");

		// The parse method variant with a channel argument is intended for
		// multi-channel devices such as USB-3003. Those devices respond with a
		// status field even for requests to change channel, e.g., PKT_CHANNEL0. The
		// status field to the actual command then follows.

		auto ch = packet.payload<StatusField>();
		if (!ch->valid(ChannelField::type(channel)))
			throw runtime_error("Received response for the wrong channel");

		if (ch->status != 0)
			throw runtime_error("Request to change channel failed");

		auto payload = packet.payload<T>(sizeof(*ch));
		if (!payload->valid(forward<Args>(args)...))
			throw runtime_error("Invalid response received");

		return payload;
	}


	template<typename T, typename... Args>
	static T* parse(const Packet& packet, Args ...args) {
		return parse<T>(packet, CONTROL, forward<Args>(args)...);
	}


	template<typename T, typename... Args>
	static T* parse(const Packet& packet, uint8_t channel, Args ...args) {
		return parse<T>(packet, CONTROL, channel, forward<Args>(args)...);
	}


	template<typename... Args>
	static bool parseStatus(const Packet& packet, Args ...args) {
		return parse<StatusField>(packet, forward<Args>(args)...)->status == 0;
	}


	template<typename... Args>
	static bool parseStatus(const Packet& packet, uint8_t channel, Args ...args) {
		return parse<StatusField>(packet, channel, forward<Args>(args)...)->status == 0;
	}

}


int Rate::parseNumber(const char* rate, int max) {
	char* end;

	errno = 0;
	auto val = strtol(rate, &end, 0);
	if (errno != 0) return -1;
	if (rate == end) return -1;
	if (val < 0 || val > max) return -1;
	return val;
}


int Rate::parseRatet(const char* rate) {
	return parseNumber(rate, 255);
}


uint16_t* Rate::parseRatep(const char* rate) {
	static uint16_t rcw[6];
	char* tmp = strdup(rate);

	int i = 0;
	char* c = strtok(tmp, ",");

	for(i = 0; i < 6; i++) {
		if (c == nullptr) goto error;

		auto v = parseNumber(c, 65535);
		if (v < 0) goto error;

		rcw[i] = v;
		c = strtok(nullptr, ",");
	}
	if (c != nullptr) goto error;

	return rcw;
error:
	free(tmp);
	return nullptr;
}


Rate::Rate(uint8_t index) : type(RATET), index(index) {
}


Rate::Rate(uint16_t* rcw) : type(RATEP) {
	memcpy(this->rcw, rcw, 6 * sizeof(this->rcw[0]));
}


Rate::Rate(const char* rate) {
	int index = parseRatet(rate);
	if (index >= 0) {
		type = RATET;
		this->index = index;
	} else {
		uint16_t* rcw = parseRatep(rate);
		if (rcw == nullptr) throw runtime_error("Invalid AMBE rate value");
		type = RATEP;
		memcpy(this->rcw, rcw, 6 * sizeof(this->rcw[0]));
	}
}


ostream& ambe::operator<<(ostream& o, const Rate& rate) {
	switch(rate.type) {
		case Rate::RATET:
			o << to_string(rate.index);
			break;

		case Rate::RATEP:
			for(int i = 0; i < 6; i++) {
				o << "0x" << setfill('0') << setw(4) << hex << rate.rcw[i];
				if (i < 5) o << ",";
			}
			break;

		default:
			throw logic_error("Bug: Invalid AMBE rate type");
	}
	return o;
}


API::API(Device& device, Scheduler& scheduler, bool check_parity) :
	device(device), scheduler(scheduler), check_parity(check_parity) {
}


void API::hardReset() {
	// Perform hardware reset of USB-3003 or USB-3012 by sending a UART break
	// signal to it.

	HardResetInterface* resettable = dynamic_cast<HardResetInterface*>(&device);
	FifoDevice* dev = dynamic_cast<FifoDevice*>(&device);

	promise<void> retval;
	auto future = retval.get_future();

	// First, we install our own callback to receive any packets from the
	// dongle. The callback ignores packets other than AMBE_READY. Once
	// AMBE_READY has been received, the callback fullfills the above promise.

	auto prev = dev->setCallback([this, &retval](const string& packet) {
		try {
			// Do not check parity when waiting for PKT_READY after a reset
			parse<Field>(Packet(move(packet), true, false), READY);
		} catch(const runtime_error& e) {
			return;
		}
		retval.set_value();
	});

	try {
		// Perform a hard reset of the dongle. Only USB-3003 dongles support
		// hard reset. To perform a hard reset, we send an UART break signal to
		// the device and then receive the AMBE_READY packet from the dongle.
		resettable->reset();

		// Wait for the promise to get resolved. That indicates that an
		// AMBE_READY packet was received.
		future.get();
	} catch(...) {
		dev->setCallback(prev);
		throw;
	}
	// Restore the original packet callback
	dev->setCallback(prev);
}


void API::softReset() {
	FifoDevice* dev = dynamic_cast<FifoDevice*>(&device);

	// First, send 350 zero characters to the AMBE chip. This will terminate any
	// previously sent unfinished packet. Inspired by DVSI's official Linux
	// client software.
	string zero(10, '\0');
	for(int i = 0; i < 3500; i++) dev->send(zero);

	// Perform a soft reset by sending the AMBE_RESET packet to the dongle.
	// Always send a RESET packet with parity on so that we can reset the
	// device no matter what state the dongle is in.
	Packet request;
	request.append<Field>(RESET);
	request.finalize(device.uses_parity);
	auto response = scheduler.submit(request).get();

	// Do not check parity when waiting for PKT_READY after a reset
	parse<Field>(response, READY);
}


void API::reset(bool hard) {
	if (hard) hardReset();
	else softReset();
	device.uses_parity = true;
}


void API::compand(bool enabled, bool alaw) {
	Packet request;
	request.append<CompandField>(enabled, alaw);
	request.finalize(device.uses_parity);

	auto response = scheduler.submit(request).get();
	if (check_parity && device.uses_parity && !response.checkParity())
		throw runtime_error("Invalid packet parity");

	if (!parseStatus(response, COMPAND))
		throw runtime_error("PKT_COMPAND request failed");
}


void API::paritymode(unsigned char mode) {
	const bool parity = mode > 0;

	Packet request;
	request.append<ParityModeField>(parity);
	request.finalize(device.uses_parity);

	// Reconfigure the device with the new parity setting before sending the
	// request so that when the response comes, the device will have the correct
	// setting. This request must not be used concurrently with other requests.

	device.uses_parity = parity;
	auto response = scheduler.submit(request).get();

	if (check_parity && device.uses_parity && !response.checkParity())
		throw runtime_error("Invalid packet parity");

	if (!parseStatus(response, PARITYMODE))
		throw runtime_error("PKT_PARITYMODE request failed");
}


// FIXME: We should check that the string is zero-terminated
string API::prodid() {
	Packet request;
	request.append<Field>(PRODID);
	request.finalize(device.uses_parity);

	auto response = scheduler.submit(request).get();
	if (check_parity && device.uses_parity && !response.checkParity())
		throw runtime_error("Invalid packet parity");

	auto fld = parse<StringField>(response, PRODID);
	return &fld->value[0];
}


// FIXME: We should check that the string is zero-terminated
string API::verstring() {
	Packet request;
	request.append<Field>(VERSTRING);
	request.finalize(device.uses_parity);

	auto response = scheduler.submit(request).get();
	if (check_parity && device.uses_parity && !response.checkParity())
		throw runtime_error("Invalid packet parity");

	auto fld = parse<StringField>(response, VERSTRING);
	return &fld->value[0];
}


void API::setMode(uint8_t channel, FieldType type, bool ns_e, bool cp_s, bool cp_e, bool dtx_e, bool td_e, bool ts_e) {
	Packet request;
	request.append<ChannelField>(channel);
	request.append<ModeField>(type, ns_e, cp_s, cp_e, dtx_e, td_e, ts_e);
	request.finalize(device.uses_parity);

	auto response = scheduler.submit(request).get();
	if (check_parity && device.uses_parity && !response.checkParity())
		throw runtime_error("Invalid packet parity");

	if (!parseStatus(response, type))
		throw runtime_error("PKT_{E,D}CMODE request on channel " + to_string(channel) + " failed");
}


void API::ecmode(uint8_t channel, bool ns_e, bool cp_s, bool cp_e, bool dtx_e, bool td_e, bool ts_e) {
	setMode(channel, ECMODE, ns_e, cp_s, cp_e, dtx_e, td_e, ts_e);
}


void API::dcmode(uint8_t channel, bool ns_e, bool cp_s, bool cp_e, bool dtx_e, bool td_e, bool ts_e) {
	setMode(channel, DCMODE, ns_e, cp_s, cp_e, dtx_e, td_e, ts_e);
}


void API::ratet(uint8_t channel, uint8_t index) {
	Packet request;

	request.append<ChannelField>(channel);
	request.append<RatetField>(index);
	request.finalize(device.uses_parity);

	auto response = scheduler.submit(request).get();
	if (check_parity && device.uses_parity && !response.checkParity())
		throw runtime_error("Invalid packet parity");

	if (!parseStatus(response, channel, RATET))
		throw runtime_error("PKT_RATET request on channel " + to_string(channel) + " failed");
}


void API::ratep(uint8_t channel, const uint16_t* rcw) {
	Packet request;
	request.append<ChannelField>(channel);
	request.append<RatepField>(rcw);
	request.finalize(device.uses_parity);

	auto response = scheduler.submit(request).get();
	if (check_parity && device.uses_parity && !response.checkParity())
		throw runtime_error("Invalid packet parity");

	if (!parseStatus(response, channel, RATEP))
		throw runtime_error("PKT_RATEP request on channel " + to_string(channel) + " failed");
}


void API::rate(uint8_t channel, const Rate& rate) {
	switch(rate.type) {
		case Rate::RATET: ratet(channel, rate.index); break;
		case Rate::RATEP: ratep(channel, rate.rcw);   break;
		default: throw logic_error("Bug: Unsupported rate type");
	}
}


void API::init(uint8_t channel, bool encoder, bool decoder) {
	Packet request;
	request.append<ChannelField>(channel);
	request.append<InitField>(encoder, decoder);
	request.finalize(device.uses_parity);

	auto response = scheduler.submit(request).get();
	if (check_parity && device.uses_parity && !response.checkParity())
		throw runtime_error("Invalid packet parity");

	if (!parseStatus(response, channel, INIT))
		throw runtime_error("PKT_INIT request on channel " + to_string(channel) + " failed");
}


future<Packet> API::compress(uint8_t channel, const int16_t* samples, size_t count) {
	Packet request(SPEECH);
	request.append<ChannelField>(channel);
	request.append<SpchdField>(count);

	auto data = request.appendArray<int16_t>(count);
	memcpy(data, samples, count * sizeof(samples[0]));

	request.finalize(device.uses_parity);
	return scheduler.submit(request);
}


future<Packet> API::decompress(uint8_t channel, const char* bits, size_t count) {
	Packet request(CHANNEL);
	request.append<ChannelField>(channel);
	request.append<ChandField>(count);

	size_t bytes = AmbeFrame::byteLength(count);
	auto data = request.appendArray<char>(bytes);
	memcpy(data, bits, bytes);

	request.finalize(device.uses_parity);
	return scheduler.submit(request);
}
