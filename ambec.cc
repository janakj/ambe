#include "ambec.h"

#include <iostream>
#include <queue>
#include <fstream>
#include <thread>
#include <cstdlib>
#include <regex>
#include <getopt.h>
#include <arpa/inet.h>
#include <byteswap.h>
#include <future>
#include <list>

#include "uri.h"
#include "rpc.h"
#include "api.h"

using namespace std;
using namespace std::chrono;
using namespace ambe;


static void printHelp() {
	cout <<
	"Usage: ambec [options]\n"
	"Options:\n"
	"  -c <number>           Number of channels to use simultaneously (all available by default)\n"
	"  -t                    Run in concurrent mode (default is synchronous mode)\n"
	"  -p <max_requests>     Request pipeline size (default is 2)\n"
	"  -i <filename>         Input data .wav file\n"
	"  -o <filename>         Optional filename to write output to\n"
	"  -u <URI>              AMBE device URI\n"
	"  -x [<index>|<rcw[6]>] AMBE_RATET index or 6 comma-delimited AMBE_RATEP values\n"
	"  -h                    This help text\n" << endl;

	exit(EXIT_FAILURE);
}


// Load audio samples from the given .wav file. The file must be in the correct
// format (8000 Hz, 1 channel, S16LE samples). The returned samples are in *big
// endian format*.
static Audio load(const string& filename) {
	size_t n = 0;
	Audio rv;
	const int mask = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

	SF_INFO info;
	auto handle = sf_open(filename.c_str(), SFM_READ, &info);
	if (!handle) {
		cerr << "Error: Could not open file for reading" << endl;
		goto error;
	}

	if (info.samplerate != SAMPLE_RATE) {
		cerr << "Error: Invalid sample rate, expected " << SAMPLE_RATE
		     << ", got " << info.samplerate << endl;
		goto error;
	}

	if (info.channels != 1) {
		cerr << "Error: Invalid number of channels, expected 1, got " << info.channels << endl;
		goto error;
	}

	if ((info.format & mask) != mask) {
		cerr << "Error: Only S16LE sample format is supported" << endl;
		goto error;
	}

	while(true) {
		AudioFrame f;
		f.fill(0);
		n = sf_read_short(handle, f.data(), f.size());
		if (n < 0) {
			cerr << "Error while reading from file: " << sf_strerror(handle) << endl;
			goto error;
		}

		swap(f.data(), f.data(), f.size());
		rv.push_back(move(f));
		if (n < f.size()) break;
	}

	sf_close(handle);
	return rv;

error:
	if (handle) sf_close(handle);
	exit(EXIT_FAILURE);
}


// Save given audio data to .wav file "filename". The input samples must be in
// *big endian* format and will be automatically converted to little endian.
static void save(const string& filename, const Audio& data) {
	SF_INFO info;
	SNDFILE* handle = NULL;
	AudioFrame tmp;

	info.samplerate = SAMPLE_RATE;
	info.channels = 1;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

	if (!sf_format_check(&info)) {
		cerr << "BUG: Format not supported by libsndfile" << endl;
		goto error;
	}

	handle = sf_open(filename.c_str(), SFM_WRITE, &info);
	if (!handle) {
		cerr << "Error: Could not open file for writing" << endl;
		goto error;
	}

	for(const auto& frame : data) {
		swap(tmp.data(), frame.data(), frame.size());
		if (sf_write_short(handle, tmp.data(), tmp.size()) != (ssize_t)frame.size()) {
			cerr << "Error while writing to file: " << sf_strerror(handle) << endl;
			goto error;
		}
	}
	sf_close(handle);
	return;
error:
	if (handle) sf_close(handle);
	exit(EXIT_FAILURE);
}


void ArgData::ProcessArgs(int argc, char* argv[]) {
	int opt = 0;
	while ((opt = getopt(argc, argv, "c:tp:i:o:u:x:h")) != -1) {
		switch (opt) {
		case 'c': channels = stoi(optarg); break;
		case 't': mode = ClientMode::CONCURRENT; break;
		case 'p': pipeline_size = stoi(optarg); break;
		case 'i': in_file = string(optarg); break;
		case 'o': out_file = string(optarg); break;
		case 'u': uri = string(optarg); break;
		case 'x': rate = Rate(optarg); break;
		case 'h': printHelp(); break;
		default: printHelp(); break;
		}
	}

	if (channels < 0 || channels > 3) {
		cout << "The AMBE chip supports up to 3 channels." << endl;
		exit(EXIT_FAILURE);
	}

	if (pipeline_size < 1) {
		cout << "Invalid pipeline size (must be >=1)" << endl;
		exit(EXIT_FAILURE);
	}
}


Client::Client(const ArgData& args, Device& device, API& api) :
	args(args), device(device), ambe(api) {
	channels = args.channels == 0 ? device.channels() : args.channels;

	cout << "Client mode: ";
	switch(args.mode) {
		case ClientMode::SYNCHRONOUS: cout << "synchronous"; break;
		case ClientMode::CONCURRENT:  cout << "concurrent";  break;
	}
	cout << endl;

	if (args.mode == ClientMode::CONCURRENT) {
		cout << "Pipeline size: " << args.pipeline_size << endl;
	}

	cout << "Found AMBE device: " << ambe.prodid() << " (" << ambe.verstring() << ")" << endl;
	cout << "Device channels: " << device.channels() << endl;

	cout << "AMBE rate: " << args.rate << endl;
	cout << "Configuring channels..." << flush;
	for (int i = 0; i < device.channels(); i++) {
		ambe.rate(i, args.rate);
		ambe.init(i);
	}
	cout << "done." << endl;

	cout << "Using channels: " << channels << endl;

	cout << "Loading audio data from " << args.in_file << "..." << flush;
	input = load(args.in_file);
	cout << "done." << endl;

	save_output = args.out_file.length() > 0;
	output = vector<Audio>(channels);

	pipeline_size = args.mode == ClientMode::CONCURRENT ? args.pipeline_size : 1;
}


duration<double> Client::CompressDecompress(Audio* output, int channel, const Audio& input) {
	size_t count;
	Audio rv;

	auto start = steady_clock::now();

	for(const auto& frame : input) {
		auto packet = ambe.compress(channel, frame.data(), frame.size()).get();
		auto bits = packet.bits(count);

		packet = ambe.decompress(channel, bits, count).get();

		if (output) {
			AudioFrame f;
			auto ptr = packet.samples(count);
			if (count != f.size())
				throw runtime_error("Insufficient number of samples");
			copy_n(ptr, count, begin(f));

			output->push_back(move(f));
		}
	}

	return steady_clock::now() - start;
}


template<typename Callable>
duration<double> Client::Compress(Callable output, int channel, const Audio& input, uint max_requests) {
	size_t count;
	queue<future<Packet>> pipeline;
	const char* bits;

	auto start = steady_clock::now();
	auto frame = input.begin();

	// Load up to max_requests packet into the AMBE device
	while(pipeline.size() < max_requests && frame != input.end()) {
		pipeline.push(ambe.compress(channel, frame->data(), frame->size()));
		frame++;
	}

	// Pop the oldest request from the pipeline and submit a new one
	while(frame != input.end()) {
		auto response = pipeline.front().get();
		pipeline.pop();

		bits = response.bits(count);
		output(bits, count);

		pipeline.push(ambe.compress(channel, frame->data(), frame->size()));
		frame++;
	}

	// Wait for all requests in the pipeline to finish
	while(pipeline.size()) {
		auto response = pipeline.front().get();
		pipeline.pop();

		bits = response.bits(count);
		output(bits, count);
	}

	auto duration = steady_clock::now() - start;

	// Indicate to the consumer that we're done
	output("", 0);
	return duration;
}


duration<double> Client::Decompress(Audio* output, int channel, const AmbeBits& input, uint max_requests) {
	queue<future<Packet>> pipeline;
	bool quit = false;
	size_t count;
	bool running = false;
	auto it = input.cbegin();

	steady_clock::time_point start;

	while(!quit || pipeline.size()) {
		if (pipeline.size() == max_requests || quit) {
			auto response = pipeline.front().get();
			pipeline.pop();

			if (output) {
				AudioFrame f;

				auto ptr = response.samples(count);
				if (count != f.size())
					throw ClientException("Invalid number of samples");
				copy_n(ptr, count, begin(f));
				output->push_back(move(f));
			}
		}

		if (!running) {
			running = true;
			start = steady_clock::now();
		}

		if (quit) continue;

		const auto& bits = *it;
		it++;
		if (bits.count == 0 || it == input.cend()) {
			// Request to terminate from the compressor thread. Set terminate true,
			// but keep running until we have data to process.
			quit = true;
		} else {
			auto future = ambe.decompress(channel, bits.data(), bits.count);
			pipeline.push(move(future));
		}
	}

	return steady_clock::now() - start;
}


 void Client::SynchronousMode() {
	vector<future<duration<double>>> results;
	cout << "Running..." << flush;

	for(uint i = 0; i < channels; i++) {
		auto rv = async(launch::async, &Client::CompressDecompress, this, save_output ? &output[i] : nullptr, i, cref(input));
		results.push_back(move(rv));
	}

	vector<duration<double>> times;
	for(auto& rv : results) times.push_back(rv.get());
	cout << "done." << endl;

	cout << "Time: ";
	for(auto& time : times) cout << time.count() << "s ";
	cout << endl;
}


AmbeBits Client::PreCompress() {
	AmbeBits bits;
	auto push = [&](auto data, auto count) {
		bits.push_back(AmbeFrame(data, count));
	};

	cout << "Pre-compressing samples..." << flush;
	auto time = Compress(push, 0, input, pipeline_size);
	cout << "done. [" << time.count() << " s]" << endl;

	return bits;
}


void Client::ConcurrentMode() {
	vector<future<duration<double>>> results;

	AmbeBits compressed_input = PreCompress();
	auto noop = [](auto data, auto count) {};

	cout << "Running..." << flush;

	for(uint i = 0; i < channels; i++) {
		auto enc = async(launch::async, &Client::Compress<decltype(noop)>, this, noop, i, cref(input), pipeline_size);
		auto dec = async(launch::async, &Client::Decompress, this, save_output ? &output[i] : nullptr, i, cref(compressed_input), pipeline_size);
		results.push_back(move(enc));
		results.push_back(move(dec));
	}

	vector<duration<double>> times;
	for(auto& rv : results) times.push_back(rv.get());
	cout << "done." << endl;

	cout << "Time: ";
	for(uint i = 0; i < times.size(); i += 2)
		cout << to_string(i / 2) << ":[" << times[i].count() << " s, " << times[i + 1].count() << " s] ";
	cout << endl;
}


void Client::SaveOutput() {
	if (!save_output) {
		cout << "Discarding audio data (no output file configured)" << endl;
		return;
	}

	regex re("(\\.[^.]+)$");
	for(uint i = 0; i < channels; i++) {
		auto path = channels > 1 ? regex_replace(args.out_file, re, "." + to_string(i) + "$1") : args.out_file;
		cout << "Writing audio data to " << path << "..." << flush;
		save(path, output[i]);
		cout << "done." << endl;
	}
}


void Client::RunUSBMode(const ArgData& args, const string& authority) {
	Usb3003 device(authority);
	MultiQueueScheduler scheduler(device, device.channels());
	API api(device, scheduler);

	device.start();
	scheduler.start();

	cout << "Resetting AMBE device..." << flush;
	api.reset(true);
	cout << "done." << endl;

	cout << "Disabling parity..." << flush;
	api.paritymode(false);
	cout << "done." << endl;

	cout << "Disabling companding..." << flush;
	api.compand(false, false);
	cout << "done." << endl;

	Client client(args, device, api);

	switch(args.mode) {
		case ClientMode::SYNCHRONOUS: client.SynchronousMode(); break;
		case ClientMode::CONCURRENT:  client.ConcurrentMode();  break;
		default: throw logic_error("Unsupported client mode"); break;
	}

	client.SaveOutput();

	scheduler.stop();
	device.stop();
}


void Client::RunGRPCMode(const ArgData& args, const string& authority) {
	cout << "Connecting to " << authority << " via gRPC" << endl;

	auto channel = grpc::CreateChannel(authority, grpc::InsecureChannelCredentials());
	RpcDevice device(channel);
	FifoScheduler scheduler(device);
	API api(device, scheduler);

	device.start();
	scheduler.start();

	Client client(args, device, api);

	switch(args.mode) {
		case ClientMode::SYNCHRONOUS: client.SynchronousMode(); break;
		case ClientMode::CONCURRENT:  client.ConcurrentMode();  break;
		default: throw logic_error("Unsupported client mode"); break;
	}

	client.SaveOutput();

	scheduler.stop();
	device.stop();
}


// AMBE client's main function.
//
// Description:
// The client's main function consist of three parts
// 1. Parse provided command line arguments.
// 2. Create a Client object.
// 3. Run data compressing and decompressing in sequential mode (by default)
//    or in thread mode if -t argument is provided.
//
// Arguments:
// -m:  Enable multi-channel compression and decompression (optional).
// -p: Enable compression and decompression pipelining (optional).
//     if it is provided then it runs pipeline version even if the user
//     provides -t argument at the same time.
// -t: Enable compression and decompression threading (optional).
// -i <input_file>: A file to read from. It must be audio file.
// -o <output_file>: A file to write decompressed data.
// -u <URI>: Ambe device URI. For example, usb:/dev/ttyUSB0.
// -x <AMBE rate index>: An AMBE rate index.
// -c <number>: A number of channels to be run. By default it is set to 3.
// -h: Show help.
int main(int argc, char* argv[]) {

	ArgData args(argc, argv);

	auto uri = URI::parse(args.uri);

	if (uri.type == UriType::USB)
		Client::RunUSBMode(args, uri.authority);
	else
		Client::RunGRPCMode(args, uri.authority);

    return 0;
}
