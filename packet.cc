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

#include "packet.h"
#include <stdexcept>

using namespace std;
using namespace ambe;


Packet::Packet(PacketType type) : buffer(sizeof(Header), '\0'), has_parity(false) {
	new (buffer.data()) Header(type, has_parity ? sizeof(ParityField) : 0);
}


Packet::Packet(const string& packet, bool has_parity, bool check_parity) : buffer(packet), has_parity(has_parity) {
	// If the packet has a parity field and we are asked to check it, do so as
	// the first step before any other processing. This allows the parser to
	// fail early in case the packet got corrupted in transit.

	if (has_parity) {
		if (packet.length() < sizeof(Header) + sizeof(ParityField))
			throw runtime_error("Packet too short to have parity field");

		auto parity = (ParityField*)(packet.data() + packet.length() - sizeof(ParityField));

		if (parity->type != PARITY)
			throw runtime_error("Invalid parity header");

		if (check_parity) {
			auto data = string_view(packet).substr(1, packet.length() - 2);
			if (ParityField::parity(data) != parity->value)
				throw runtime_error("Invalid packet parity");
		}
	}

	// Make sure all fields in the packet header have sane values.
	Header::check(packet);
}


bool Packet::checkParity() {
	auto hdr = parity();

	if (!hdr->valid())
		throw runtime_error("Parity field not found at the end of packet");

	auto data = string_view(buffer).substr(1, buffer.length() - 2);
	return ParityField::parity(data) == hdr->value;
}


Header* Packet::header() const {
	return (Header*)buffer.data();
}


ParityField* Packet::parity() const {
	if (!has_parity)
		throw logic_error("No parity header");

	// Make sure the packet is big enough to actually have a Parity field at the
	// end. Do not use payloadLength() here. That method requires a valid
	// header, but this method can be called before the header has been checked.

	if (buffer.length() < sizeof(Header) + sizeof(ParityField))
		throw runtime_error("Packet too short to have parity field");

	ParityField* hdr = (ParityField*)(buffer.data() + buffer.length() - sizeof(ParityField));
	return hdr;
}


uint16_t Packet::payloadLength() const {
	return buffer.length() - sizeof(Header) - (has_parity ? sizeof(ParityField) : 0);
}


size_t Packet::length() const {
	return buffer.length();
}


void Packet::updateHeaderLength() {
	header()->setLength(buffer.length() - sizeof(Header));
}



const string& Packet::finalize(bool with_parity) {
	ParityField* field = nullptr;

	if (has_parity && !with_parity) {
		buffer.resize(buffer.length() - sizeof(ParityField));
		has_parity = false;
	} else if (!has_parity && with_parity) {
		field = append<ParityField>();
		has_parity = true;
	} else if (has_parity) {
		field = parity();
	}

	updateHeaderLength();

	if (has_parity) {
		auto data = string_view(buffer).substr(1, buffer.length() - 2);
		field->value = field->parity(data);
	}
	return buffer;
}


const string& Packet::data() const {
	return buffer;
}


PacketType Packet::type() const {
	return header()->type;
}


/**
 * Return the channel number the packet is for
 *
 * For all packet types, we return a valid channel number if and only if the
 * first field in the payload is one of CHANNEL0, CHANNEL1, or CHANNEL2. In
 * other cases the method returns -1.
 *
 * In theory, a single packet can contain fields for multiple channels. However,
 * this program does not send our packets. If a packet carries fields for
 * multiple channels, the method returns the first channel found.
 */
unsigned int Packet::channel() const {
	auto field = payload<Field>();
	switch(field->type) {
	case (int)FieldType::CHANNEL0:
	case (int)FieldType::CHANNEL1:
	case (int)FieldType::CHANNEL2:
		return field->type - (int)FieldType::CHANNEL0;
	default:
		return -1;
	}
}
