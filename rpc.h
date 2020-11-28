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

#include <memory>
#include <string>
#include <grpc++/grpc++.h>
#include "device.h"
#include "ambe.grpc.pb.h"
#include "queue.h"

using namespace std;

namespace ambe {

	class RpcDevice : public TaggingDevice {
	public:
		int channel;

		RpcDevice(shared_ptr<grpc::ChannelInterface> channel);

		virtual void start() override;
		virtual void stop() override;

		virtual int channels() const override;

		virtual TaggedCallback setCallback(TaggedCallback recv) override;
		virtual void send(int32_t tag, const string& packet) override;

	private:
		bool terminating;
		void packetReceiver();

		TaggedCallback recv;

		unique_ptr<rpc::AmbeService::Stub> stub;
		grpc::ClientContext context;
		unique_ptr<grpc::ClientReaderWriter<rpc::Packet, rpc::Packet>> stream;

		thread receiver;
	};
}
