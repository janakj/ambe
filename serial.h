#pragma once

#include <sys/types.h>
#include <vector>
#include <mutex>
#include <functional>
#include <thread>
#include <queue>
#include "device.h"
#include "queue.h"

using namespace std;

namespace ambe {

	/**
	 * A base class for UART (RS-232) based AMBE devices
	 *
	 * This class provide common code for all UART-based (i.e., RS-232) AMBE
	 * devices. It assumes the device is connected to the host via a
	 * USB-to-serial dongle such as FT-232. That's the case for DVSI's USB-3000,
	 * USB-3003, and USB-3012 devices. The implementation communicates with the
	 * device via the Linux's ftdi_sio driver and it enables the low latency
	 * mode in the driver.
	 *
	 * The implementation reads packets from the device on a dedicated thread.
	 * The callback function provided by upper layers will be invoked on that
	 * thread.
	 */
	class UartDevice : public FifoDevice {
	public:
		UartDevice(const string& pathname, int baudrate);

		virtual void start() override;
		virtual void stop() override;

		virtual FifoCallback setCallback(FifoCallback recv) override;
		virtual void send(const string& packet) override;

	protected:
		void packetReceiver(void);
		bool readPacket(string& buffer);
		bool safeCancellableRead(string& buffer, size_t n);

		string pathname;
		int baudrate;

		FifoCallback recv;

		int rfd, wfd;
		int rpipe, wpipe;
		thread receiver;
	};


	/**
	 * Driver class for DVSI's USB-3003 devices
	 *
	 * Use this class to communicate with DVSI's USB-3003 devices. Each device
	 * provides three independent channel. Also, USB-3003 devices can be
	 * hardware reset via the UART break signal, so this class implements the
	 * reset method as well.
	 */
	class Usb3003 final : public UartDevice, public HardResetInterface {
	public:
		Usb3003(const string& pathname);
		virtual int channels() const override;
		virtual void reset() override;
	};


	/**
	 * Driver class for DVSI's USB-3003 devices
	 *
	 * Use this class to communicate with DVSI's USB-3000 devices. Each device
	 * provides one channel. The USB-3000 does not support hardware reset, so
	 * the reset method raises a "not imlemented" exception when invoked.
	 */
	class Usb3000 final : public UartDevice {
	public:
		Usb3000(const string& pathname);
		virtual int channels() const override;
	};
}