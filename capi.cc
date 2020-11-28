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

#include "capi.h"
#include <stdlib.h>
#include <grpc++/grpc++.h>
#include "uri.h"
#include "rpc.h"
#include "serial.h"

using namespace std;
using namespace ambe;

#ifdef __cplusplus
extern "C" {
#endif


struct Client {
	shared_ptr<grpc::Channel> channel;
	RpcDevice* device = nullptr;
	Scheduler* scheduler = nullptr;
	API* api = nullptr;
	int deadline;
};


void* ambe_open(const char* uri, const char* rate, int deadline) {
	auto u = URI::parse(uri);

	if (u.type != UriType::GRPC)
		throw logic_error("Currently, only gRPC devices are supported");

	Client* c = NULL;

	try {
		auto c = new Client;
		c->deadline = deadline;

		c->channel = grpc::CreateChannel(u.authority, grpc::InsecureChannelCredentials());
		c->device = new RpcDevice(c->channel);
		c->scheduler = new FifoScheduler(*c->device);
		c->api = new API(*c->device, *c->scheduler);

		c->device->start();
		c->scheduler->start();

		c->api->rate(c->device->channel, Rate(rate));
		c->api->init(c->device->channel);
		cout << "ambe: Using channel " << c->device->channel << endl;
		return c;
	} catch(...) {
		ambe_close(c);
		throw;
	}
}


void ambe_close(void *handle) {
	Client* c = static_cast<Client*>(handle);

	if (c) {
		if (c->scheduler) c->scheduler->stop();
		if (c->device) c->device->stop();

		if (c->api) delete c->api;
		if (c->scheduler) delete c->scheduler;
		if (c->device) delete c->device;
		delete c;
	}
}


int ambe_compress(char* bits, size_t* bit_count, void* handle, const int16_t* samples, size_t sample_count) {
	Client* c = static_cast<Client*>(handle);
	size_t n;

	AudioFrame frame;
	if (sample_count != frame.size())
		throw logic_error("Only " + to_string(frame.size()) + " sample frames are supported");

	swap(frame.data(), samples, sample_count);

	auto future = c->api->compress(c->device->channel, frame.data(), sample_count);
	auto status = future.wait_for(chrono::milliseconds(c->deadline));
	if (status != future_status::ready) return -1;

	// Note: we need to create the packet object here on the stack to ensure
	// that the pointer returned by packet.bits() remains valid until the memcpy
	// command below is reached.
	auto packet = future.get();
	auto ptr = packet.bits(n);
	if ((*bit_count) < n) throw logic_error("Destionation buffer too small to hold AMBE bits");

	memcpy(bits, ptr, AmbeFrame::byteLength(n));
	*bit_count = n;
	return 0;
}


int ambe_decompress(int16_t* samples, size_t* sample_count, void* handle, const char* bits, size_t bit_count) {
	Client* c = static_cast<Client*>(handle);
	size_t n;

	auto future = c->api->decompress(c->device->channel, bits, bit_count);
	auto status = future.wait_for(chrono::milliseconds(c->deadline));
	if (status != future_status::ready) return -1;

	// Note: we need to create the packet object here on the stack to ensure
	// that the pointer returned by packet.samples() remains valid until the
	// memcpy command below is reached.
	auto packet = future.get();
	auto ptr = packet.samples(n);
	if ((*sample_count) < n) throw logic_error("Destionation buffer too small to hold an audio frame");

	swap(samples, ptr, n);
	*sample_count = n;
	return 0;
}

#ifdef __cplusplus
}
#endif
