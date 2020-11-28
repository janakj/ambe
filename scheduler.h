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
#include <sys/types.h>
#include <thread>
#include <future>
#include <optional>
#include <unordered_map>
#include "device.h"
#include "queue.h"
#include "packet.h"

using namespace std;

namespace ambe {
	class TaggingDevice;
	class FifoDevice;

	typedef function<void (const Packet& packet)> ResponseCallback;
	typedef tuple<Packet, optional<ResponseCallback>> State;

	/**
	 * AMBE request scheduler base class
	 *
	 * The AMBE chip sends a response upon each request it receives. Since
	 * requests and responses carry no data that would allow correlating them,
	 * the AMBE chip must send responses in the same order in which it received
	 * the requests. Processing a request takes non-trivial amount of time and
	 * there is an input buffer with limited size in the chip.
	 *
	 * An AMBE request scheduler determines the order in which requests are sent
	 * to the AMBE chip. The scheduler's goal is to maximize utilization of all
	 * channels within the chip, while keeping the input buffer size manageable.
	 * The scheduler buffers requests from clients and determines the best order
	 * in which the requests can be sent to the AMBE chip.
	 */
	class Scheduler {
	public:
		virtual ~Scheduler() {}

		/**
		 * Start the scheduler
		 *
		 * Most scheduler implementations use a background thread which is
		 * created in the start method. Here, the scheduler will also subscribe
		 * to receive responses from the device.
		 */
		virtual void start() {}

		/**
		 * Stop the scheduler
		 *
		 * If the scheduler created a thread, this is where it will be stopped.
		 * Also unsubscribe from the AMBE device in this method.
		 *
		 * This method is meant to terminate the scheduler cleanly, waiting for
		 * any requests submitted prior to the stop request to terminate.
		 */
		virtual void stop() {}

		/**
		 * Submit a request to the device, receive response via the future object
		 *
		 * Use this method to send a request to the device. The future value
		 * will be set once a response has been received from the device. The
		 * future value will be fullfilled with an exception if the request
		 * cannot be sent to the device.
		 *
		 * All implementations of the request method must be non-blocking. In
		 * particular, the method must not wait for the packet to be written to
		 * the device, i.e., the write must be performed in the background.
		 *
		 * NOTE: This method cannot be used to send requests for which the AMBE
		 * dongle generates no response (there are a couple).
		 */
		virtual future<Packet> submit(const Packet& packet);
		virtual void submitAsync(const Packet& packet, ResponseCallback callback) = 0;
	};


	/**
	 * A simple First In First Out (FIFO) request scheduler
	 *
	 * This class implements the simplest possible request scheduler for AMBE
	 * devices. The scheduler sends packets to the device in the order in which
	 * they arrive and it assumes that the AMBE device will generate reponses in
	 * the same order. Internally, the scheduler keeps a queue of submitted
	 * requests and whenever a packet is received from the device, it is
	 * associated with the request packet at the front of the queue.
	 *
	 * The future value may be satisfied with an exception if the scheduler
	 * fails to write the packet to the device.
	 */
	class FifoScheduler final : public Scheduler {
	public:
		FifoScheduler(TaggingDevice& device);

		virtual void start() override;
		virtual void stop() override;

		void submitAsync(const Packet& packet, ResponseCallback callback) override;

	private:
		void recv(int32_t tag, const string& packet);

		TaggingDevice& device;
		int32_t tag;

		std::mutex mutex;
		unordered_map<int32_t, ResponseCallback> submitted;

		bool quit;
		promise<void> terminated;
	};


	/**
	 * A request scheduler for AMBE devices with multiple channels (pipelines)
	 *
	 * This is a request scheduler for DVSI's AMBE-3000 and AMBE-3003 devices.
	 * These devices have two independent CPU cores per channel (2 and 6 CPU
	 * cores respectively).
	 *
	 * This request scheduler maintains one queue per CPU core, plus one extra
	 * queue for device control requests. Incoming packets are assigned the
	 * queue corresponding to their channel and type of operation (compression /
	 * decompression). When selecting the next request to send to the device,
	 * the scheduler picks a queue to maximize the utilization of all CPU cores,
	 * making sure that the input buffer remains filled but not overloaded.
	 *
	 * Control requests that operate on the entire device (as opposed to a
	 * single channel) are prioritized and sent to the device as soon as space
	 * in the device's input buffer is available.
	 */
	class MultiQueueScheduler final : public Scheduler {
	public:
		static const unsigned int queues_per_channel = 2;
		static const unsigned int max_channels = 3;

		MultiQueueScheduler(FifoDevice& device, unsigned int channels);

		void start() override;
		void stop() override;

		void submitAsync(const Packet& packet, ResponseCallback callback) override;

	private:

		void recv(const string& packet);
		void run();

		unsigned int queued() const;
		int queueIndex(const Packet& request) const;
		unsigned int typeIndex(const Packet& request) const;
		bool canSend(const Packet& request) const;

		FifoDevice& device;
		thread runner;

		SyncQueue<State> process;

		// A separate high-priority queue for AMBE device control requests.
		queue<State> device_queue;

		unsigned int channels;
		vector<queue<State>> channel_queue;

		// A queue of requests that have been submitted to the AMBE device but
		// for which we have not received a response yet.
		queue<State> submitted;

		// The number of requests on the submitted queue broken down by packet
		// type.
		array<unsigned int, (int)PacketType::MAX> submitted_by_type{0};

		// The number of requests on the submitted queue broken down by channel
		// queues.
		vector<unsigned int> submitted_by_queue;
	};
}
