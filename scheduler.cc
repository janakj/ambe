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

#include "scheduler.h"
#include <iostream>
#include <mutex>
#include <cstring>
#include <memory>
#include <list>
#include "api.h"

using namespace std::placeholders;
using namespace ambe;
using namespace std;


future<Packet> Scheduler::submit(const Packet& packet) {
	auto rv = make_shared<promise<Packet>>();
	auto future = rv->get_future();

	auto callback = [rv](const Packet& response) {
		rv->set_value(move(response));
	};

	submitAsync(packet, callback);
	return future;
}


FifoScheduler::FifoScheduler(TaggingDevice& device) : device(device) {
}


void FifoScheduler::start() {
	quit = false;
	tag = 0;
	terminated = promise<void>();
	device.setCallback(bind(&FifoScheduler::recv, this, _1, _2));
}


void FifoScheduler::stop() {
	unique_lock<std::mutex> lock(mutex);

	// If we have outstanding requests on the queue, wait for all of them to get
	// closed. Set a flag and a promise for the receiving thread to resolve once
	// the queue is empty.

	if (!submitted.empty()) {
		quit = true;
		auto future = terminated.get_future();
		lock.unlock();
		future.get();
	}

	device.setCallback(nullptr);
}


void FifoScheduler::submitAsync(const Packet& packet, ResponseCallback callback) {
	// Note: we can lock the mutex before device.send() because the method is
	// guaranteed to be non-blocking, i.e., it will not block until the packet
	// is written to the device.

	lock_guard<std::mutex> lock(mutex);
	try {
		device.send(++tag, packet.data());
	} catch(...) {
		callback(Packet());
	}

	submitted[tag] = callback;
}


void FifoScheduler::recv(int32_t tag, const string& packet) {
	lock_guard<std::mutex> lock(mutex);

	auto v = submitted.find(tag);
	if (v == submitted.end()) {
		cerr << "Warning: Received response with unknown tag" << endl;
		return;
	}

	v->second(Packet(move(packet), device.uses_parity, false));
	submitted.erase(v);

	if (quit && submitted.empty())
		terminated.set_value();
}


MultiQueueScheduler::MultiQueueScheduler(FifoDevice& device, unsigned int channels) : device(device) {
	if (channels > max_channels)
		throw std::logic_error("Invalid number of channels: " + to_string(channels));

	this->channels = channels;
	int queues = channels * queues_per_channel;
	channel_queue = vector<queue<State>>(queues);
	submitted_by_queue = vector<unsigned int>(queues, 0);
}


void MultiQueueScheduler::start() {
	device.setCallback(bind(&MultiQueueScheduler::recv, this, _1));
	runner = thread(&MultiQueueScheduler::run, this);
}


void MultiQueueScheduler::stop() {
	// Terminate the background thread by giving it an empty packet to transmit.
	// Wait until we get a response. That indicates that all requests buffered
	// before this one have been completed.
	Scheduler::submit(Packet()).get();
	runner.join();

	device.setCallback(nullptr);
}


void MultiQueueScheduler::submitAsync(const Packet& packet, ResponseCallback callback) {
	process.push(make_tuple(packet, move(callback)));
}


// The recv method will be called on whatever thread the device uses to receive
// packets.

void MultiQueueScheduler::recv(const string& packet) {
	process.push(make_tuple(Packet(move(packet), device.uses_parity, false), nullopt));
}


unsigned int MultiQueueScheduler::typeIndex(const Packet& request) const {
	// Control packets always get 0. These are either packets for the entire
	// chip (such packets carry no channel information), or they will be queued
	// in the first queue corresponding to the channel.

	switch(request.type()) {
	case PacketType::CHANNEL: return 1;
	case PacketType::SPEECH:  return 0;
	case PacketType::CONTROL: return 0;
	default: throw logic_error{"Unsupported packet type"};
	}
}


int MultiQueueScheduler::queueIndex(const Packet& request) const {
	int channel = request.channel();

	// If we get a -1 then the packet does not have a channel field and it is
	// probably a packet for the whole device (e.g., something like AMBE_RESET).
	// If that's the case, indicate to the caller to put the packet into the
	// per-device queue by returning -1.

	if (channel == -1) return -1;
	return queues_per_channel * channel + (typeIndex(request));
}


bool MultiQueueScheduler::canSend(const Packet& request) const {
	// The input buffer can store up to four packets. Two of those can be SPEECH
	// packets and two can be CHANNEL packets. Thus, the maximum number of
	// packets that can be submitted to the chip at any time is the number of
	// channel queues (one packet from each can be processing) plus four
	// additional packets.
	if (submitted.size() >= channel_queue.size() + 4) return false;

	// The input buffer is big enough to store only two CHANNEL and two SPEECH
	// packets at the same time. Thus, the maximum number of either CHANNEL or
	// SPEECH packets that can be submitted at any time is the number of
	// channels (each channel can be processing one) plus two. Control packets
	// are lumped together with SPEECH packets since such packets are processed
	// immediately and don't keep the chip busy.
	if (submitted_by_type[typeIndex(request)] >= channels + 2) return false;

	// If any channel runs out of data, the above two checks will overcommit and
	// might write too many packets into the input buffer. Here we also make
	// sure that, at any given time, no more than 2 packets per queue have been
	// submitted. The CPU core can be processing one and the other packet will
	// be waiting in the input buffer.
	int i = queueIndex(request);
	if (i > 0 && (submitted_by_queue[i] >= 2)) return false;

	// If all the above conditions are satisfied, we can submit the given packet
	// to the AMBE chip.
	return true;
}


unsigned int MultiQueueScheduler::queued() const {
	unsigned int rv = device_queue.size();
	for(const auto& q : channel_queue) rv += q.size();
	return rv;
}


void MultiQueueScheduler::run() {
	unsigned int next = 0;
	bool quit = false;
	unsigned int queued = 0;
	optional<ResponseCallback> terminated;

	while (!quit || queued || submitted.size()) {
		auto tuple = move(process.pop());
		const auto& packet = get<0>(tuple);
		auto& callback = get<1>(tuple);

		if (!packet.payloadLength()) {
			// If it is an empty packet, set a flag to terminate once all
			// data has been processed and discard it.
			quit = true;

			// Notify the stop method once the thread has stopped
			if (callback) terminated = callback;
		} else if (callback) {
			// We got a new request to transmit to the AMBE chip. File it in
			// the appropriate queue.
			auto i = queueIndex(packet);
			if (i == -1) device_queue.push(move(tuple));
			else channel_queue[i].push(move(tuple));
			queued++;
		} else {
			// We got a new response from the AMBE chip
			if (!submitted.empty()) {
				auto& tuple = submitted.front();
				const auto& request = get<0>(tuple);
				auto& callback = get<1>(tuple);

				int i = queueIndex(request);
				if (i != -1) {
					submitted_by_type[typeIndex(request)]--;
					submitted_by_queue[queueIndex(request)]--;
				}
				// If we have (an optional) promise associated with the request,
				// fullfill it with the response packet that we just received.
				if (callback) callback.value()(packet);

				submitted.pop();
			}
		}

		// First transmit any packets on the high-priority queue 0. Those are
		// control packets for the entire AMBE device (and not for a specific
		// channel).

		while(!device_queue.empty()) {
			const auto& request = get<0>(device_queue.front());

			if (!canSend(request)) break;

			device.send(request.data());

			submitted.push(move(device_queue.front()));
			device_queue.pop();
			queued--;
		}

		unsigned int queues = channel_queue.size();
		for (unsigned int j = 0; (j < queues) && queued; j++, next = (next + 1) % queues) {
			if (channel_queue[next].empty()) continue;

			const auto& request = get<0>(channel_queue[next].front());
			if (!canSend(request)) continue;

			device.send(request.data());

			submitted_by_type[typeIndex(request)]++;
			submitted_by_queue[queueIndex(request)]++;
			submitted.push(move(channel_queue[next].front()));
			channel_queue[next].pop();
			queued--;

			j = 0;
		}
	}

	if (terminated) terminated.value()(Packet());
}
