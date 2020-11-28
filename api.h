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

#include <stddef.h>
#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <vector>

#include "queue.h"
#include "device.h"
#include "scheduler.h"
#include "packet.h"

using namespace std;

#define SAMPLE_RATE 8000
#define FRAME_DURATION 20
#define FRAME_SIZE 8000 / 1000 * FRAME_DURATION


namespace ambe {
	class Device;
	class Scheduler;

	// A single frame, i.e., 20 ms of audio data in linear 16-bit *big endian*
	// format.
	typedef array<int16_t, FRAME_SIZE> AudioFrame;

	extern void swap(int16_t* dst, const int16_t* src, size_t count);


	// A single frame of AMBE (compressed) bits. These bits represent a single
	// AudioFrame. The number of bits depends on the selected AMBE rate index.
	class AmbeFrame {
		string buffer;

	public:
		uint count;

		static uint byteLength(uint count) {
			return count / 8 + (count % 8 > 0);
		}

		AmbeFrame(const char* data, uint count) :
			buffer(data, byteLength(count)), count(count) {}

		AmbeFrame() : buffer(), count(0) {}

		const char* data() const {
			return buffer.data();
		}
	};

	typedef deque<AudioFrame> Audio;
	typedef deque<AmbeFrame> AmbeBits;


	class Rate {
	public:
		enum { RATET, RATEP } type;
		union {
			uint8_t index;
			uint16_t rcw[6];
		};

	private:
		static int parseNumber(const char* rate, int max);
		static int parseRatet(const char* rate);
		static uint16_t* parseRatep(const char* rate);

	public:
		Rate(uint8_t index);
		Rate(uint16_t* rcw);
		Rate(const char* rate);

		ostream& operator<<(ostream& o);
	};

	ostream& operator<<(ostream& o, const Rate& rate);


	class API {
	private:
		Device& device;
		Scheduler& scheduler;
		bool check_parity;

		void setMode(uint8_t channel, FieldType type, bool ns_e, bool cp_s, bool cp_e, bool dtx_e, bool td_e, bool ts_e);

		int toBytes(int bits);

		void hardReset();
		void softReset();

	public:
		API(Device& device, Scheduler& scheduler, bool check_parity=true);

		string prodid();
		string verstring();

		void reset(bool hard=false);

		/* Enable or disable parity fields at the end of every packet. If mode is 0
		* then parity fields will be disabled for all output packets beginning with
		* the response to this packet. The AMBE-3003™ will not require a valid
		* parity byte for future received packets. If mode is 1 then parity fields
		* will be enabled for all output packets beginning with the response to
		* this packet. The AMBE-3003™ will reject all future received packets that
		* do not have a valid parity field. All other values for mode are reserved
		* and should not be used.
		*/
		void paritymode(unsigned char mode);

		void compand(bool enabled,  bool alaw);

		/* ns_e  : Noise Suppression Enable
		* cp_s  : Compand Select
		* cp_e  : Compand Enable
		* dtx_e : Discontinuous Transmit Enable
		* td_e  : Tone Detection Enable
		* ts_e  : Tone Send Enable
		*/
		void ecmode(uint8_t channel, bool ns_e, bool cp_s, bool cp_e, bool dtx_e, bool td_e, bool ts_e);

		/* ns_e  : Noise Suppression Enable
		* cp_s  : Compand Select
		* cp_e  : Compand Enable
		* dtx_e : Discontinuous Transmit Enable
		* td_e  : Tone Detection Enable
		* ts_e  : Tone Send Enable
		*/
		void dcmode(uint8_t channel, bool ns_e, bool cp_s, bool cp_e, bool dtx_e, bool td_e, bool ts_e);

		/* The AMBE-3003™ vocoder chip can support up to three channels of
		* encode/decode data. The number of channels the chip can support is
		* dependant on the data and FEC rates used. The rate of the AMBE-3003™ can
		* be set through hardware pins or control words. After resetting the
		* device, the coding rate can be modified for both the encoder and the
		* decoder by sending a PKT_RATET or PKT_RATEP packet. The following table
		* shows standard Rate / FEC combinations and how many channels they can
		* support in real-time.
		*
		*                     AMBE-1000™ Rates
		* Index #   Total Rate   Speech Rate   FEC Rate  Channels
		*  0        2400         2400             0      3
		*  1        3600         3600             0      3
		*  2        4800         3600          1200      2
		*  3        4800         4800             0      3
		*  4        9600         9600             0      3
		*  5        2400         2350            50      2
		*  6        9600         4850          4750      2
		*  7        4800         4550           250      2
		*  8        4800         3100          1700      2
		*  9        7200         4400          2800      2
		* 10        6400         4150          2250      2
		* 11        3600         3350           250      2
		* 12        8000         7750           250      2
		* 13        8000         4650          3350      2
		* 14        4000         3750           250      2
		* 15        4000         4000             0      3
		*
		*                     AMBE-2000™ Rates
		* Index #   Total Rate   Speech Rate   FEC Rate  Channels
		* 16        3600         3600             0      3
		* 17        4000         4000             0      3
		* 18        4800         4800             0      3
		* 19        6400         6400             0      3
		* 20        8000         8000             0      3
		* 21        9600         9600             0      3
		* 22        4000         2400          1600      2
		* 23        4800         3600          1200      2
		* 24        4800         4000           800      2
		* 25        4800         2400          2400      2
		* 26        6400         4000          2400      2
		* 27        7200         4400          2800      2
		* 28        8000         4000          4000      2
		* 29        9600         2400          7200      2
		* 30        9600         3600          6000      2
		* 31        2000         2000             0      3
		* 32        6400         3600          2800      2
		*
		*                     AMBE-3003™ Rates
		* Index #   Total Rate   Speech Rate   FEC Rate  Channels
		* 33        3600         2450          1150      3
		* 34        2450         2450             0      3
		* 35        3400         2250          1150      2
		* 36        2250         2250             0      3
		* 37        2400         2400             0      3
		* 38        3000         3000             0      3
		* 39        3600         3600             0      3
		* 40        4000         4000             0      3
		* 41        4400         4400             0      3
		* 42        4800         4800             0      3
		* 43        6400         6400             0      3
		* 44        7200         7200             0      3
		* 45        8000         8000             0      3
		* 46        9600         9600             0      3
		* 47        2700         2450           250      2
		* 48        3600         3350           250      2
		* 49        4000         3750           250      2
		* 50        4800         4550           250      2
		* 51        4400         2450          1950      2
		* 52        4800         2450          2350      2
		* 53        6000         2450          3550      2
		* 54        7200         2450          4750      2
		* 55        4000         2600          1400      2
		* 56        4800         3600          1200      2
		* 57        4800         4000           800      2
		* 58        6400         4000          2400      2
		* 59        7200         4400          2800      2
		* 60        8000         4000          4000      2
		* 61        9600         3600          6000      2
		*
		* Rate Index #32 is compatible with the AMBE-2000™ Vocoder chip however; it
		* is not part of the AMBE-2000™ Vocoder chip standard rate table.
		*
		* Index rates #32 to #63 are AMBE+2 mode rates.
		*
		* Index rate #33 is interoperable with APCO P25 Half Rate and DMR (Europe).
		*
		* To enable the P.25 half-rate mode with FEC (3600 bit/s), use ambe_ratet
		* with index 33.
		*
		* To enable the P.25 half-rate mode without FEC (2450 bit/s), use
		* ambe_ratet with index 34.
		*/
		void ratet(uint8_t channel, uint8_t index);

		/* To enable the P.25 full-rate mode with FEC (7200 bits/s), configure the
		* chip with ambe_ratep using the following rate words:
		* 0x0558 0x086b 0x1030 0x0000 0x0000 0x0190
		*
		* To enable the P.25 full-rate mode without FEC (4400 bit/s), use the
		* following rate words:
		* 0x0558 0x086b 0x0000 0x0000 0x0000 0x0158
		*/
		void ratep(uint8_t channel, const uint16_t* rcw);

		void rate(uint8_t channel, const Rate& rate);

		void init(uint8_t channel, bool encoder=true, bool decoder=true);

		future<Packet> compress(uint8_t channel, const int16_t* samples, size_t count);
		future<Packet> decompress(uint8_t channel, const char* bits, size_t count);

	};
}
