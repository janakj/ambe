#pragma once

#include <iostream>
#include <cstring>
#include <exception>
#include <sndfile.hh>

#include "serial.h"
#include "api.h"
#include "queue.h"
#include "device.h"


using namespace std;

namespace ambe {
	using namespace std::chrono;

	enum class ClientMode {
		SYNCHRONOUS,
		CONCURRENT
	};

	struct ArgData {
		ClientMode mode = ClientMode::SYNCHRONOUS;
		string in_file;
		string out_file;
		string uri;
		Rate rate;
		int channels = 0;
		DeviceMode device_mode = DeviceMode::USB;
		int pipeline_size = 2;

	public:
		ArgData(int argc, char* argv[]) : rate(33) {
			ProcessArgs(argc, argv);
		}
		~ArgData() {}

	private:
		void ProcessArgs(int argc, char* argv[]);
	};


	// Client provides an interface to compress and decompress
	// data using API services which communicate with the AMBE device.
	//
	// Data Members:
	// - dev: The AMBE device object. Note that it is thread safe.
	// - scheduler: AMBE device request scheduler
	// - ambe: An instance of AMBE API class connected to the device via the scheduler
	class Client {
		ArgData args;

		Device& device;
		API& ambe;
		unsigned int channels;

		int pipeline_size;

		Audio input;

		bool save_output;
		vector<Audio> output;

	public:
		// Method constructs an Client object by doing following steps:
		// 1. Initializes built in type variables with corresponding values.
		// 2. Reset and initialize AMBE device.
		// 3. Configure all channels on the AMBE device.
		//
		// Method throws an exception if one of the above steps fails.
		Client(const ArgData& args, Device& device, API& api);


		// Run compression and decompression in synchronous mode.
		//
		// This method compresses and decompresses given audio frames
		// synchronously, i.e., one at a time. If no output pointer is provided,
		// the result is discarded.
		duration<double> CompressDecompress(Audio* output, int channel, const Audio& input);

		// Method reads data from a file, chunk by chunk, compresses data and
		// then stores compressed buffer to the queue so the decompression method
		// can read it and decompress. This method is designed to run as a
		// separate thread.
		//
		// Note that it converts s16le to s16be before passing data to compression
		// as the chip inside the dongle is big endian and expects 16-bit audio
		// samples.
		template<typename Callable>
		duration<double> Compress(Callable output, int channel, const Audio& input, uint pipeline_size);

		// Method reads data from the queue (compressor thread pushes data to this
		// queue), decompresses it and writes to a file. This method is designed to
		// run as a separate thread.
		//
		// Note that it converts s16be to s16le before storing data to a file
		// as the chip inside the dongle is big endian and decompression output
		// is s16be.
		duration<double> Decompress(Audio* output, int channel, const AmbeBits& input, uint pipeline_size);

		void SaveOutput();
		AmbeBits PreCompress();

		// Static method for running client in two different modes: USB and GRPC
		static void RunUSBMode(const ArgData& args, const string& authority);
		static void RunGRPCMode(const ArgData& args, const string& authority);

		void SynchronousMode();
		void ConcurrentMode();
	};


	class ClientException : public exception {
		string msg = "ambe::ClientException";
	public:
		ClientException(const char* message = "") {
			msg += string(message);
		}
		~ClientException() {}

		const char* what () const throw () {
			return msg.c_str();
		}
	};
}
