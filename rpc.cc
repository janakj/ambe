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

#include "rpc.h"
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <queue>
#include <chrono>

#include <grpcpp/grpcpp.h>
#include "ambe.grpc.pb.h"
#include "api.h"
#include "device.h"

using namespace std;
using namespace ambe;


RpcDevice::RpcDevice(shared_ptr<grpc::ChannelInterface> channel) : stub(rpc::AmbeService::NewStub(channel)) {
	stream = nullptr;
}


void RpcDevice::start() {
	terminating = false;

	stream = stub->bind(&context);
	stream->WaitForInitialMetadata();

	auto attrs = context.GetServerInitialMetadata();
	auto ch = attrs.find("channel");
	auto up = attrs.find("uses_parity");

	if (ch == attrs.cend() || up == attrs.cend()) {
		stream->WritesDone();
		stream->Finish();
		throw runtime_error("Error while connecting to gRPC server");
	}

	channel = stoi(ch->second.data());
	uses_parity = stoi(up->second.data());

	receiver = thread(&RpcDevice::packetReceiver, this);
}


void RpcDevice::stop() {
	terminating = true;

	// Indicate to the server that we have no more packets to send
	stream->WritesDone();

	// Wait for the server to return the final status for the call to getChannel()
	auto status = stream->Finish();
	if (!status.ok())
		throw runtime_error(status.error_message());

	// Wait for the packet reader thread to terminate
	receiver.join();
}


int RpcDevice::channels() const {
	return 1;
}


TaggedCallback RpcDevice::setCallback(TaggedCallback recv) {
	TaggedCallback old = this->recv;
	this->recv = recv;
	return old;
}


void RpcDevice::send(int32_t tag, const string& packet) {
	rpc::Packet pkt;
	pkt.set_tag(tag);
	pkt.set_data(packet);
	if (!stream->Write(pkt))
		throw runtime_error("Error while sending packet");
}


void RpcDevice::packetReceiver() {
	rpc::Packet packet;

	while(stream->Read(&packet))
		if (recv) recv(packet.tag(), packet.data());

	// If the connection to the server got close due to a reason other than the
	// caller invoking the stop() method, report an error. We cannot easily
	// re-establish the call because that would involve invoking getChannel()
	// again which could potentiallly return a different channel. It's best to
	// let the higher layer application code to handle the error.

	if (!terminating)
		throw runtime_error("Lost connection to gRPC server");
}
