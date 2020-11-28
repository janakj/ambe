# A library for working with DVSI's AMBE vocoder chips

This project develops tools for interfacing with Advanced Multiband Excitation (AMBE) hardware vocoder chips developed by Digital Voice Systems, Inc. (DVSI). The proprietary AMBE technology is used in many existing industrial communication systems such as [Project 25](http://www.project25.org/), [Digital Mobile Radio (DMR)](https://www.etsi.org/technologies/mobile-radio), and [Globalstar](https://www.globalstar.com/en-us/).

The following three components are available:
  * **libambe**: A shared library that can be used from third-party programs
  * **ambed**: A transcoding server with a [gRPC](https://grpc.io/) interface
  * **ambec**: A command line utility for testing, development, and offline transcoding

The library has been designed for DVSI's [family of USB devices](https://www.dvsinc.com/products/usb_3k.shtml#interop) and has been tested with [USB-3000 and USB-3003](https://www.dvsinc.com/products/usb_3k.shtml). In general, the code should be easy to adapt for any device based on the family of [AMBE-3000](https://www.dvsinc.com/products/a300x.shtml) chips with only a minimal effort.

This project was created by [Jan Janak](https://www.cs.columbia.edu/~janakj), [Artiom Baloian](mailto:ab4659@columbia.edu), and [Henning Schulzrinne](https://www.cs.columbia.edu/~hgs).

## Installation

If you only wish to build the shared library for use in your programs, please make sure the following dependencies are installed: [Protocol Buffers](https://github.com/google/protobuf/) libraries and compiler, [gRPC](https://github.com/google/protobuf/) libraries, gRPC plugins for the Protocol Buffers compiler. On a Debian-based Linux distributions you can install everything as follows:
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

## License

This project is licensed under the [GNU General Public License v3](https://www.gnu.org/licenses/gpl-3.0.en.html). Please see the file [LICENSE](./LICENSE) for more details.
