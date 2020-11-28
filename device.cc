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

#include "device.h"
#include <iostream>

using namespace ambe;
using namespace std;


DeviceManager::DeviceManager(const string& id, Device& device, Scheduler& scheduler) {
	add(id, device, scheduler);
}


DeviceManager::~DeviceManager() {
/*
	 // TODO Not sure if it makes sense to stop here.
	for (auto& device : devices) {
		std::get<0>(device.second).stop();
		std::get<1>(device.second).stop();
	}
*/
}


void DeviceManager::add(const string& id, Device& device, Scheduler& scheduler) {
	if (devices.find(id) == devices.end()) {
		vector<bool> channels(device.channels(), false);
		devices.insert({id, forward_as_tuple(ref(device), ref(scheduler), channels)});
		// TODO Not sure if it makes sense to start here.
		// std::get<0>(devices[id]).start();
		// std::get<1>(devices[id]).start();
	} else {
		throw runtime_error("AMBE chip " + id + " already added");
	}
}


pair<string, size_t> DeviceManager::acquireChannel() {
	for (auto& device : devices) {
		auto& channels = get<2>(device.second);
		for (size_t i = 0; i < channels.size(); i++) {
			if (!channels[i]) {
				channels[i] = true;
				return make_pair(device.first, i);
			}
		}
	}

	throw runtime_error("No channels left");
}


void DeviceManager::releaseChannel(const string& id, size_t channel) {
	if (!deviceExists(id)) throw runtime_error("Channel releasing error. AMBE chip " + id + " not found");

	auto it = devices.find(id);
	auto& channels = get<2>(it->second);
	if (channel < channels.size() && channel >= 0)
		channels[channel] = false;
	else
		throw runtime_error("Provided channel number " + to_string(channel) + " is not supported");
}


tuple<Device&, Scheduler&, vector<bool>>* DeviceManager::getData(const string& id) {
	auto it = devices.find(id);
	if (it != devices.end()) return &it->second;
	return nullptr;
}


bool DeviceManager::deviceExists(const string& id) {
	if (devices.find(id) == devices.end()) return false;
	return true;
}
