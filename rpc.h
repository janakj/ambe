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
