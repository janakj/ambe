#pragma once

#include <sys/types.h>
#include <memory>
#include <future>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <tuple>

#include "api.h"
#include "queue.h"
#include "scheduler.h"

#define MAX_CHANNELS 3

using namespace std;

namespace ambe {
	class Channel;
	class Scheduler;
	class API;

	typedef function<void(const string&)> FifoCallback;
	typedef function<void(int32_t, const string&)> TaggedCallback;

	enum class DeviceMode {
		USB = 0,
		GRPC = 1
	};


	/**
	 * An abstract AMBE device
	 *
	 * This is an abstract base class that represents various kinds of AMBE
	 * dongle devices, both locally-connected such as USB-3000, USB-3003, or
	 * USB-3012, and remote connected over the network. Any derived class must
	 * provide a means to:
	 *  - obtain the number of channels provided by the device
	 *  - set a callback function to process incoming packets
	 *  - send a packet to the device, and to reset the device
	 */
	class Device {
	public:
		virtual ~Device() {}

		bool uses_parity = true;

		/**
		 * Start the device
		 *
		 * This method will be invoked by the program when it wishes to
		 * initialize the device and start receiving packets from it. Method
		 * setCallback can be called before start(). All other method must be
		 * called after start.
		 */
		virtual void start() = 0;

		/**
		 * Stop the device
		 *
		 * This method will be called by the program before the object is
		 * destroyed. After stop() has been called, the device must no longer
		 * invoke the callback set with setCallback.
		 */
		virtual void stop() = 0;

		/**
		 * Return the number of supported channels
		 *
		 * This method returns the number of channels supported by the device.
		 * The number depends on the hardware variant. For example, USB-3000
		 * supports only one channel, USB-3003 supports 3 channels, and a
		 * USB-3012 device would return 12 available channels.
		 */
		virtual int channels() const = 0;
	};


	class HardResetInterface {
	public:
		/**
		 * Reset the device
		 *
		 * Implementations should provide this method to allow the program to
		 * perform hardware device reset. Since not all AMBE devices support
		 * hardware reset, this  method may throw an exception if it is called
		 * on a device that cannot be hard-reset.
		 *
		 * This method can block while it is resetting the device.
		 */
		virtual void reset() = 0;
	};


	// AMBE devices preserve the order of requests and responses. That is, the
	// device will send responses in the same order in which requests were
	// received. This interface captures that semantic. The callback provided in
	// setCallback will receive responses in the order in which requests were
	// submitted using the send method.

	class FifoDevice : public Device {
	public:
		/**
		 * Set a callback function to receive packets from the device
		 *
		 * The callback function set through this method will be invoked
		 * whenever the device sends a packet to the host. The callback function
		 * may be invoked from a different thread. Returns the previous callback
		 * if any or nullptr.
		 */
		virtual FifoCallback setCallback(FifoCallback recv) = 0;

		/**
		 * Send a packet to the device
		 *
		 * Send a packet stored in string "packet" to the device. This method is
		 * blocking and is not thread-safe. In particular, an implementation can
		 * block until the packet has been transferred to the AMBE chip.
		 * Invoking the method concurrently from multiple threads results in
		 * undefined behavior.
		 *
		 * NOTE: Currently, there is no way to find out whether the driver
		 * succesfully wrote the packet to the device. If an error is thrown
		 * while the packet is being written to the device, the implementation
		 * is expected to throw an exception that will terminate the whole
		 * process. Due to the nature of the serial protocol between the AMBE
		 * chip and the host, write errors are hard to recover from correctly
		 * and it might be best to reset the entire process if one occurs.
		 */
		virtual void send(const string& packet) = 0;
	};


	// When talking a remote AMBE device over the network, the FIFO semantic may
	// no longer be preserved because the device is most likely going to reorder
	// requests from multiple clients to guarantee fairness. This interface
	// captures thats semantic and allows the client to submit a unique tag with
	// each request that will be returned by the device with the response,
	// allowing the client to correlated requests and responses based on the tag
	// value.

	class TaggingDevice : public Device {
	public:
		/**
		 * Set a callback function to receive packets from the device
		 *
		 * The callback function set through this method will be invoked
		 * whenever the device sends a packet to the host. The callback function
		 * may be invoked from a different thread. Returns the previous callback
		 * if any or nullptr.
		 */
		virtual TaggedCallback setCallback(TaggedCallback recv) = 0;

		/**
		 * Send a packet to the device
		 *
		 * Send a packet stored in string "packet" to the device. This method is
		 * blocking and is not thread-safe. In particular, an implementation can
		 * block until the packet has been transferred to the AMBE chip.
		 * Invoking the method concurrently from multiple threads results in
		 * undefined behavior.
		 *
		 * NOTE: Currently, there is no way to find out whether the driver
		 * succesfully wrote the packet to the device. If an error is thrown
		 * while the packet is being written to the device, the implementation
		 * is expected to throw an exception that will terminate the whole
		 * process. Due to the nature of the serial protocol between the AMBE
		 * chip and the host, write errors are hard to recover from correctly
		 * and it might be best to reset the entire process if one occurs.
		 */
		virtual void send(int32_t tag, const string& packet) = 0;
	};


	class DeviceManager {
	public:
		DeviceManager();
		DeviceManager(const string& id, Device& device, Scheduler& scheduler);
		~DeviceManager();

		void add(const string& id, Device& device, Scheduler& scheduler);

		pair<string, size_t> acquireChannel();
		void releaseChannel(const string& id, size_t channel);

		tuple<Device&, Scheduler&, vector<bool>>* getData(const string& id);

	private:
		unordered_map<string, tuple<Device&, Scheduler&, vector<bool>>> devices;
		bool deviceExists(const string& id);
	};
}
