#pragma once
#include <string>
#include <string_view>
#include <stdexcept>
#include <algorithm>
#include <memory>
#include <cstdlib>
#include <byteswap.h>
#include <arpa/inet.h>

using namespace std;


namespace ambe {

	enum __attribute__ ((packed)) StartByte {
		START_BYTE = 0x61
	};

	enum __attribute__ ((packed)) PacketType {
		CONTROL = 0x00,
		CHANNEL = 0x01,
		SPEECH  = 0x02,
		MAX     = 0x03
	};

	enum __attribute__ ((packed)) FieldType {
		SPCHD        = 0x00,  // Field carries speech samples
		CHAND        = 0x01,  // Field carries AMBE channel bits
		ECMODE       = 0x05,  // Encoder cmode flags for current channel
		DCMODE       = 0x06,  // Decoder cmode flags for current channel
		RATET        = 0x09,  // Select rate from table for current channel
		RATEP        = 0x0a,  // Select custom rate for current channel
		INIT         = 0x0b,  // Initialize encoder and/or decoder for current channel
		LOWPOWER     = 0x10,  // Enable or disable low-power mode
		CHANFMT      = 0x15,  // Sets the format of the output Channel Packet
		SPCHFMT      = 0x16,  // Sets the format of the output Speech Packet
		PARITY       = 0x2f,  // Per-packet parity field
		PRODID       = 0x30,  // Query for product identification
		VERSTRING    = 0x31,  // Query for product version string
		COMPAND      = 0x32,  // Companding ON/OFF and a-law/µ-law selection
		RESET        = 0x33,  // Reset the device using hard configuration via pins
		RESETSOFTCFG = 0x34,  // Reset the device with software configuration
		HALT         = 0x35,  // Sets AMBE-3003™ into lowest power mode
		GETCFG       = 0x36,  // Query for configuration pin state at power-up or reset
		READCFG      = 0x37,  // Query for current state of configuration pins
		READY        = 0x39,  // Indicates that the device is ready to receive packets
		PARITYMODE   = 0x3f,  // Enable (default) / disable parity fields
		CHANNEL0     = 0x40,  // The subsequent fields are for channel 0
		CHANNEL1     = 0x41,  // The subsequent fields are for channel 1
		CHANNEL2     = 0x42,  // The subsequent fields are for channel 2
		DELAYNUS     = 0x49,  // Delays the next control field processing (in microsecs)
		DELAYNNS     = 0x4a,  // Delays the next control field processing (in nanosecs)
		GAIN         = 0x4b,  // Used to set Input gain and output gain to be anywhere between +90 and -90 dB
		RTSTHRESH    = 0x4e   // Sets the flow control thresholds
	};


	struct __attribute__ ((packed)) Header {
		const uint8_t start_byte = START_BYTE;
	private:
		uint16_t length;
	public:
		const PacketType type;

		uint16_t getLength() const {
			return ntohs(length);
		}

		void setLength(uint16_t val) {
			length = htons(val);
		}

		Header(PacketType type=CONTROL, uint16_t len=0) : type(type) {
			setLength(len);
		}

		static void check(const string& packet) {
			if (packet.length() < sizeof(Header))
				throw runtime_error("Packet too short to have header");

			const Header* hdr = (Header*)packet.data();

			if (hdr->start_byte != START_BYTE)
				throw runtime_error("Invalid packet start byte");

			if (hdr->getLength() != packet.length() - sizeof(Header))
				throw runtime_error("Invalid packet length");

			switch(hdr->type) {
			case PacketType::CONTROL:
			case PacketType::CHANNEL:
			case PacketType::SPEECH:
				break;
			default:
				throw runtime_error("Invalid packet type");
			}
		}
	};

	static_assert(sizeof(Header) == 4);


	struct __attribute__ ((packed)) Field {
		const FieldType type;
		Field(FieldType type) : type(type) {}

		bool valid(FieldType type_) const {
			return type == type_;
		}
	};

	static_assert(sizeof(Field) == 1);


	struct __attribute__ ((packed)) ParityField : Field {
		uint8_t value;   // Parity calculated over the packet, excluding Header::start_byte and Parity::value
		ParityField() : Field(PARITY) {}

		static uint8_t parity(const string_view& data) {
			uint8_t v = 0;
			for(uint8_t c : data) v ^= c;
			return v;
		}

		bool valid() const {
			return Field::valid(PARITY);
		}
	};

	static_assert(sizeof(ParityField) == sizeof(Field) + 1);


	struct __attribute__ ((packed)) ChannelField : Field {
		static FieldType type(uint8_t channel) {
			return (FieldType)(CHANNEL0 + channel);
		}

		ChannelField(uint8_t channel) : Field(type(channel)) {
			if (channel > 2) throw logic_error("Invalid channel number");
		}

		bool valid(uint8_t channel) const {
			if (channel > 2) throw logic_error("Invalid channel number");
			return Field::valid(type(channel));
		}

		bool valid() const {
			return Field::valid(CHANNEL0)
				|| Field::valid(CHANNEL1)
				|| Field::valid(CHANNEL2);
		}
	};

	static_assert(sizeof(ChannelField) == sizeof(Field));




	struct __attribute__ ((packed)) SpchdField : Field {
		uint8_t samples;   // Number of 16-bit speech samples
		int16_t data[0];   // An array of samples
		SpchdField(uint8_t samples) : Field(SPCHD), samples(samples) {}
	};

	static_assert(sizeof(SpchdField) == sizeof(Field) + 1);


	struct __attribute__ ((packed)) ChandField : Field {
		uint8_t bits;  // Number of bits stored in data
		char data[0];  // A bit representation of AMBE-compressed frame
		ChandField(uint8_t bits) : Field(CHAND), bits(bits) {}
	};

	static_assert(sizeof(ChandField) == sizeof(Field) + 1);


	struct __attribute__ ((packed)) StatusField : Field {
		const uint8_t status = 0;
		StatusField(FieldType type) = delete;
	};

	static_assert(sizeof(StatusField) == sizeof(Field) + 1);


	struct __attribute__ ((packed)) CompandField : Field {
	private:
		uint8_t param;
	public:
		CompandField(bool enabled, bool alaw) : Field(COMPAND) {
			setEnabled(enabled);
			setAlaw(alaw);
		}

		void setEnabled(bool state) {
			if (state) param |= 1;
			else param &= ~1;
		}

		void setAlaw(bool state) {
			if (state) param |= 2;
			else param &= ~2;
		}
	};

	static_assert(sizeof(CompandField) == sizeof(Field) + 1);


	struct __attribute__ ((packed)) ParityModeField : Field {
		const uint8_t mode;
		ParityModeField(uint8_t mode) : Field(PARITYMODE), mode(mode) {}
	};

	static_assert(sizeof(ParityModeField) == sizeof(Field) + 1);


	struct __attribute__ ((packed)) StringField : Field {
		char value[0];
		StringField(FieldType type) = delete;
	};

	static_assert(sizeof(StringField) == sizeof(Field));


	struct __attribute__ ((packed)) RatetField : Field {
		const uint8_t index;
		RatetField(uint8_t index) : Field(RATET), index(index) {}
	};

	static_assert(sizeof(RatetField) == sizeof(Field) + 1);


	struct __attribute__ ((packed)) InitField : Field {
	private:
		uint8_t params;
	public:
		InitField(bool encoder, bool decoder) : Field(INIT) {
			params = (decoder ? 2 : 0) + (encoder ? 1 : 0);
		}
	};

	static_assert(sizeof(InitField) == sizeof(Field) + 1);


	struct __attribute__ ((packed)) RatepField : Field {
	private:
		uint16_t rcw[6];
	public:
		RatepField(const uint16_t* rcw_) : Field(RATEP) {
			for(int i = 0; i < 6; i++) rcw[i] = htons(rcw_[i]);
		}
	};

	static_assert(sizeof(RatepField) == sizeof(Field) + 12);


	struct __attribute__ ((packed)) ModeField : Field {
	private:
		uint8_t params;
	public:
		ModeField(FieldType type, bool ns_e, bool cp_s, bool cp_e, bool dtx_e, bool td_e, bool ts_e) :
			Field(type) {
			params = (ns_e  << 6)
				   | (cp_s  << 7)
				   | (cp_e  << 8)
				   | (dtx_e << 11)
				   | (td_e  << 12)
				   | (ts_e  << 14);
		}
	};

	static_assert(sizeof(ModeField) == sizeof(Field) + 1);


	class Packet {
		string buffer;
		bool has_parity;

		void updateHeaderLength();

	public:
		Packet(PacketType type=CONTROL);
		Packet(const string& packet, bool has_parity, bool check_parity);

		bool checkParity();

		Header* header() const;
		ParityField* parity() const;
		PacketType type() const;
		uint16_t payloadLength() const;
		size_t length() const;
		const string& data() const;
		const string& finalize(bool with_parity=true);
		unsigned int channel() const;

		template<typename FieldClass>
		FieldClass* payload(size_t offset=0) const {
			if ((payloadLength() - offset) < sizeof(FieldClass)) {
				throw runtime_error("Packet too short to have given payload type");
			}

			return (FieldClass*)(buffer.data() + sizeof(Header) + offset);
		}

		template<typename FieldClass, typename... FieldArgs>
		FieldClass* append(FieldArgs ...args) {
			auto old = buffer.length();
			buffer.resize(old + sizeof(FieldClass));
			return new (buffer.data() + old) FieldClass(forward<FieldArgs>(args)...);
		}

		template<typename T>
		T* appendArray(size_t count) {
			auto old = buffer.length();
			buffer.resize(old + sizeof(T) * count);
			return new (buffer.data() + old) T[count];
		}

		const int16_t* samples(size_t& count) const {
			if (type() != SPEECH)
				throw runtime_error("Speech packet expected");

			auto channel = payload<ChannelField>();
			if (!channel->valid())
				throw runtime_error("Invalid packet channel");

			auto spchd = payload<SpchdField>(sizeof(ChannelField));
			count = spchd->samples;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
			return &spchd->data[0];
#pragma GCC diagnostic pop
		}

		const char* bits(size_t& count) const {
			if (type() != CHANNEL)
				throw runtime_error("Channel packet expected");

			auto ch = payload<ChannelField>();
			if (!ch->valid())
				throw runtime_error("Invalid packet channel");

			auto chand = payload<ChandField>(sizeof(ChannelField));
			count = chand->bits;
			return &chand->data[0];
		}
	};
}