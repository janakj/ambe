# A library for working with DVSI's AMBE vocoder chips

This project develops tools for interfacing with Advanced Multiband Excitation (AMBE) hardware vocoder chips developed by Digital Voice Systems, Inc. (DVSI). The proprietary AMBE technology is used in many existing industrial communication systems such as [Project 25](http://www.project25.org/), [Digital Mobile Radio (DMR)](https://www.etsi.org/technologies/mobile-radio), and [Globalstar](https://www.globalstar.com/en-us/).

The following three components are available:
  * **libambe**: A shared library that can be used from third-party programs
  * **ambed**: A transcoding server with a [gRPC](https://grpc.io/) interface
  * **ambec**: A command line utility for testing, development, and offline transcoding

The library has been designed for DVSI's [family of USB devices](https://www.dvsinc.com/products/usb_3k.shtml#interop) and has been tested with [USB-3000 and USB-3003](https://www.dvsinc.com/products/usb_3k.shtml). In general, the code should be easy to adapt for any device based on the family of [AMBE-3000](https://www.dvsinc.com/products/a300x.shtml) chips with only a minimal effort.

This project was created by [Jan Janak](https://www.cs.columbia.edu/~janakj), [Artiom Baloian](mailto:ab4659@columbia.edu), and [Henning Schulzrinne](https://www.cs.columbia.edu/~hgs).

## Installation

If you only wish to build the shared library for use in your programs, please make sure the following dependencies are installed: [Protocol Buffers](https://github.com/google/protobuf/) libraries and compiler, [gRPC](https://github.com/google/protobuf/) libraries, gRPC plugins for the Protocol Buffers compiler. On Debian-based Linux distributions you can install everything as follows:
```sh
apt install libprotobuf-dev  protobuf-compiler libgrpc-dev libgrpc++-dev protobuf-compiler-grpc
```
then build and install the shared library only:
```sh
make lib
make install-libs
```
The shared library requires Protocol Buffers and gRPC because it includes support remote transcoding with `ambed`.

If you also wish build and install the server (`ambed`):
```sh
make server
make install-server
```

To build the command line client (`ambec`), make sure [libsndfile](http://www.mega-nerd.com/libsndfile/) is installed and run:
```sh
make client
make install-client
```

## Usage

### C Language API
The shared library comes with a `pkg-config` configuration file which you can use to configure your build environment:
```sh
$ pkg-config --libs --cflags libambe
-pthread -I/usr/local//include -L/usr/local//lib -lambe
```
In your C program, initialize the library as follows:
```c
#include <ambe/capi.h>

void* handle = ambe_open(URI, RATE, DEADLINE);
if (handle == NULL) {
    /* report error and abort */
}
```
The argument `URI` identifies the device to use. To communicate with a locally attached USB dongle, the string should be of the form `usb://dev/<char_device>`, for example, `usb:/dev/ttyUSB0`. If you wish to communicate with a remote `ambed` based vocoder over gRPC, the string should be of the form `grpc:<host_or_ip>:<port>`.

The string argument `RATE` select the rate to be configured in the vocoder chip. If you provide a single number, the corresponding mode will be selected using the command `PKT_RATET`. If you provide a comma-separate list of six numbers, the parameters will be passed to the command `PKT_RATEP`. For a list of supported values, please refer to the reference documentation for your AMBE vocoder chip.

The integer argument `DEADLINE` configures the maximum time a compression/decompression operation can take in milliseconds. This argument is mainly useful in gRPC mode. When talking to a local device via USB, configure a large enough value, e.g., 100ms.

To encode an audio frame, invoke the function `ambe_compress` as follows:
```c
char* bits;
size_t bit_count;
int16_t* samples;
size_t sample_count;

ambe_compress(buffer, &bit_count, handle, samples, sample_count);
```
where `buffer` is the destination buffer to store compressed bits. The buffer must be large enough for your selected mode. The number of bits written into the buffer will be stored in the variable `count`. The input data must contain 20 milliseconds of S16LE samples sampled with 8000 Hz.

To decode a frame of AMBE-encoded bits, invoke `ambe_decompress` as follows:
```c
ambe_decompress(samples, &sample_count, handle, bits, bit_count);
```
The decoded sample data will be stored in the variable `samples` which must point to a buffer large enough. The number of samples will be written to `sample_count`.

Both functions return 0 on success, a negative number on error (timeout).

Invoke `ambe_close` to release any resources that might be held by the library when your program is done compressing/decompressing:
```c
ambe_close(handle);
```



## License

This project is licensed under the [GNU General Public License v3](https://www.gnu.org/licenses/gpl-3.0.en.html). Please see the file [LICENSE](./LICENSE) for more details.
