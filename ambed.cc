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

#include <iostream>
#include <exception>
#include <memory>
#include <string>
#include <chrono>
#include <getopt.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include "ambe.grpc.pb.h"
#include "queue.h"
#include "api.h"
#include "device.h"
#include "serial.h"

using namespace std;
using namespace ambe;

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using grpc::ServerReaderWriter;


static unsigned short port = 50051;
static string pathname;


class AmbeServiceImpl final : public rpc::AmbeService::Service {
public:
	explicit AmbeServiceImpl(const string& pathname) :
		device(pathname), scheduler(device, 3), api(device, scheduler), dev_manager(pathname, device, scheduler) {
		id = pathname;
		device.start();
		scheduler.start();
		initChip();
	}

private:
	void initChip() {
		cout << "Resetting AMBE chip " << id << "..." << flush;
		api.reset(true);
		cout << "done." << endl;

		cout << "Found AMBE chip " << api.prodid()
				<< " version " << api.verstring()
				<< endl;

		std::cout << "Disabling parity in AMBE chip " << id << "..." << std::flush;
		api.paritymode(false);
		std::cout << "done." << std::endl;

		cout << "Disabling companding in AMBE chip " << id << "..." << flush;
		api.compand(false, false);
		cout << "done." << endl;
	}


	Status bind(ServerContext* context, ServerReaderWriter<rpc::Packet, rpc::Packet>* stream) override {
		pair<string, size_t> channel;
		try {
			channel = dev_manager.acquireChannel();
		} catch(const runtime_error& e) {
			return Status(StatusCode::UNAVAILABLE, "No channels left");
		}
		context->AddInitialMetadata("channel", grpc::to_string(channel.second));

		context->AddInitialMetadata("uses_parity", grpc::to_string(device.uses_parity));
		stream->SendInitialMetadata();

		rpc::Packet request;
		while(stream->Read(&request)) {
			const auto tag = request.tag();

			auto callback = [tag, stream](const Packet& packet) {
				rpc::Packet response;
				response.set_tag(tag);
				response.set_data(packet.data());

				// FIXME: What happens if the the stream object becomes invalid,
				// e.g., because the client closed the connection, while the
				// lambda is waiting to be invoked?
				if (!stream->Write(response))
					throw runtime_error("Error while sending response");
			};

			scheduler.submitAsync(Packet(request.data(), device.uses_parity, false), callback);
		}

		dev_manager.releaseChannel(channel.first, channel.second);
		return Status::OK;
	}


	Status ping(ServerContext* context, ServerReaderWriter<rpc::Ping, rpc::Ping>* stream) override {
		rpc::Ping ping;
		while (stream->Read(&ping)) stream->Write(ping);
		return Status::OK;
	}


	string id;
	Usb3003 device;
	MultiQueueScheduler scheduler;
	API api;

	DeviceManager dev_manager;
};


static void print_help(void) {
	static char help_msg[] = "\
Usage: ambed [options]\n\
Options:\n\
    -h         This help text.\n\
    -p <num>   Port number to listen on.\n\
    -s <path>  Serial port with an AMBE chip.\n\
";

	fprintf(stdout, "%s", help_msg);
	exit(EXIT_SUCCESS);
}


int main(int argc, char** argv) {
	int opt;

	while((opt = getopt(argc, argv, "hvp:s:")) != -1) {
		switch(opt) {
		case 'h': print_help();        break;
		case 'p': port = atoi(optarg); break;
		case 's':
			pathname = string(optarg);
			break;
		default:
			fprintf(stderr, "Use the -h option for list of supported "
					"program arguments.\n");
			exit(EXIT_FAILURE);
		}
	}

	if (port < 0 || port > 65535) {
		fprintf(stderr, "Invalid port number: %d\n", port);
		exit(EXIT_FAILURE);
	}

	if (!pathname.length()) {
		fprintf(stderr, "Please provide a serial port (see -h)\n");
		exit(EXIT_FAILURE);
	}

	string addr("0.0.0.0:" + to_string(port));

	ServerBuilder builder;

	AmbeServiceImpl service(pathname);
	builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
	builder.RegisterService(&service);
	unique_ptr<Server> server(builder.BuildAndStart());

	cout << "AMBE gRPC server listening on " << addr << endl;
	server->Wait();

	return EXIT_SUCCESS;
}
