#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "serial.h"
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/serial.h>

#include "api.h"
#include <iostream>

using namespace std::placeholders;
using namespace std;
using namespace ambe;


static bool lockFile(int fd) {
	struct flock lock;

	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;

	if (fcntl(fd, F_SETLK, &lock) == -1) {
		if (errno == EACCES || errno == EAGAIN) return false;
		throw system_error(errno, system_category());
	}

	return true;
}


static void setNonblocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1) goto error;

	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) goto error;
	return;
error:
	throw system_error(errno, system_category());
}


/**
 * Configure low-latency mode for a serial port
 *
 * Some USB-to-serial adapters such as FT232 add about 16 ms of delay to bytes
 * coming from the serial port by default. This delay reduces the load on the
 * USB subsystem and the host CPU. Linux provides a special ioctl that can be
 * used to lower this delay. This function can be used to enable or disable a
 * so-called low-latency mode. In low-latency mode, the delay added to incoming
 * data is set to 1 ms instead of 16 ms.
 *
 * An FT232-based USB-to-serial adapter is built into all DVSI's USB-based
 * devices.
 *
 * This function is Linux-specific.
 */
static void setLowLatency(int fd, bool yesno) {
	struct serial_struct serial;

	if (ioctl(fd, TIOCGSERIAL, &serial) < 0) goto error;

	if (yesno) serial.flags |= ASYNC_LOW_LATENCY;
	else serial.flags &= ~ASYNC_LOW_LATENCY;

	if (ioctl(fd, TIOCSSERIAL, &serial) < 0) goto error;
	return;

error:
	cerr << "setLowLatency: " << strerror(errno) << endl;
	throw system_error(errno, system_category());
}


static void initTTY(int fd, int baudrate) {
	struct termios tty;

	if (tcgetattr(fd, &tty) < 0) goto error;

	if (cfsetspeed(&tty, (speed_t)baudrate) < 0) goto error;

	// The following settings configure the terminal in raw (non-canonical) mode

	tty.c_cflag |= (CLOCAL | CREAD); // Enable local mode (there is no modem)
	tty.c_cflag &= ~CSIZE;           // Clear character size related bits
	tty.c_cflag |= CS8;              // 8 bits per character
	tty.c_cflag &= ~PARENB;          // No parity bit
	tty.c_cflag &= ~CSTOPB;          // 1 stop bit
	tty.c_cflag |= CRTSCTS;          // Enable hardware flow control

	tty.c_iflag &= ~(IGNBRK | BRKINT);       // Flush queues on BREAK, do not generate SIGINT
	tty.c_iflag &= ~(IGNPAR | PARMRK);       // Output '\0' character for framing or parity errors
	tty.c_iflag &= ~(INLCR | IGNCR | ICRNL); // No CR LF translation
	tty.c_iflag &= ~(IXON | IXOFF); 	     // disable XON/XOFF flow control

	tty.c_oflag &= ~OPOST; // Disable any special output processing

	tty.c_lflag &= ~ICANON;          // Disable canonical mode
	tty.c_lflag &= ~(ECHO | ECHONL); // Do not echo input characters
	tty.c_lflag &= ~ISIG;            // Do not generate signals
	tty.c_lflag &= ~IEXTEN;          // Disable any special input processing

	// Fetch individual bytes as soon as they become available

	tty.c_cc[VMIN] = 1;
	tty.c_cc[VTIME] = 0;

	if (tcsetattr(fd, TCSANOW, &tty) != 0) goto error;
	return;

error:
	cerr << "initTTY: " << strerror(errno) << endl;
	throw system_error(errno, system_category());
}


/**
 * Make sure that all given data have been written to fd
 *
 * Invoke the write system call repeatedly until all data have been written to
 * the given file descriptor or an unrecoverable error has been encountered.
 *
 * Returns the number of characters written. An error was encountered if the
 * returned number is smaller than n.
 */
static size_t writeAll(int fd, const void* buffer, size_t n) {
	size_t len;
	ssize_t rc;

	len = 0;
	while(len < n) {
		do {
			rc = write(fd, (char*)buffer + len, n - len);
		} while (rc < 0 && errno == EINTR);

		if (rc <= 0) break;
		len += rc;
	}
	return len;
}


static void flushBuffers(int fd) {
	if (tcflush(fd, TCIOFLUSH) < 0) {
		cerr << "tcflush: " << strerror(errno) << endl;
		throw system_error(errno, system_category());
	}
}


UartDevice::UartDevice(const string& pathname, int baudrate) : pathname(pathname) {
	this->baudrate = baudrate;
	recv = nullptr;
}


void UartDevice::start() {
	int pipefd[2];

	if (pipe(pipefd) > 0)
		throw system_error(errno, system_category());

	rpipe = pipefd[0];
	wpipe = pipefd[1];

	rfd = -1;
	wfd = -1;
	try {
		cout << "Opening serial port " << pathname
			 << " (baud rate " << baudrate << ")"
			 << endl;

		wfd = open(pathname.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
		if (wfd < 0) throw system_error(errno, system_category());

		if (!lockFile(wfd)) {
			cerr << "Error: Failed to lock serial port " << pathname
				 << " (another process using it?)"
				 << endl;
			throw runtime_error("Failed to lock serial port");
		}

		initTTY(wfd, baudrate);

		rfd = dup(wfd);
		if (rfd < 0) throw system_error(errno, system_category());
		setNonblocking(rfd);

		// Enable low latency mode on the serial port (Linux only)
		setLowLatency(rfd, true);

		// Discard any data in the ports input and output buffer before starting
		// reader/writer threads.
		//
		// The following wait time is a kludge needed to discard any data in
		// input/output buffers of USB-to-serial adapters. Unfortunately, with
		// USB-to-serial devices there is no bullet-proof way to implement the
		// flushing.
		//
		// See: https://bugzilla.kernel.org/show_bug.cgi?id=5730

		usleep(1000);
		if (tcflush(wfd, TCIOFLUSH) < 0) {
			cerr << "tcflush: " << strerror(errno) << endl;
			throw system_error(errno, system_category());
		}

		receiver = thread(&UartDevice::packetReceiver, this);
	} catch(...) {
		close(rpipe);
		close(wpipe);

		if (rfd >= 0) {
			try {
				setLowLatency(rfd, false);
			} catch(...) {}
			close(rfd);
		}
		if (wfd >= 0) close(wfd);
		throw;
	}
}


void UartDevice::stop() {
	if (write(wpipe, "Q", 1) <= 0)
		cerr << "[" << pathname << "] Error while stopping packet receiver thread: " << strerror(errno) << endl;

	receiver.join();

	setLowLatency(rfd, false);
	close(rfd);
	close(wfd);
	close(rpipe);
	close(wpipe);
}


FifoCallback UartDevice::setCallback(FifoCallback recv) {
	FifoCallback old = this->recv;
	this->recv = recv;
	return old;
}


void UartDevice::send(const string& packet) {
	// If we fail to write the packet to the device for some reason,
	// terminate the program. A failure to write a packet typically
	// indicates a serious problem and it may take a device reset and
	// full re-initialization to recover from it.
	unsigned int len = packet.length();
	if (writeAll(wfd, packet.c_str(), len) != len)
		throw system_error(errno, system_category());
}


void UartDevice::packetReceiver(void) {
	string buffer;

	try {
		while (readPacket(buffer)) {
			if (recv) recv(buffer);
		}
	} catch(...) {
		cout << "[" << pathname << "] Packet receiver thread terminated with an exception" << endl;
		throw;
	}
}


bool UartDevice::readPacket(string& buffer) {
	Header *h;

	// Read the packet incrementally. First, read the input byte by byte until
	// we find a start byte. Then read the fixed-size header to obtain the total
	// length of the packet. Finally, read the rest of the packet based on the
	// length attribute in the header.

	do {
		buffer.clear();
		if (!safeCancellableRead(buffer, 1)) return false;

		h = (Header*)buffer.c_str();
	} while(h->start_byte != START_BYTE);

	if (!safeCancellableRead(buffer, sizeof(Header) - 1))
		return false;

	h = (Header*)buffer.c_str();
	uint16_t len = h->getLength();

	if (!safeCancellableRead(buffer, len)) return false;
	return true;
}


bool UartDevice::safeCancellableRead(string& buffer, size_t n) {
	size_t len;
	ssize_t rc;
	fd_set fds;

	len = buffer.length();
	buffer.resize(buffer.length() + n);
	char* start = (char*)buffer.c_str() + len;

	int maxfd = (rfd > rpipe ? rfd : rpipe) + 1;

	FD_ZERO(&fds);
	FD_SET(rfd, &fds);
	FD_SET(rpipe, &fds);

	len = 0;
	while(len < n) {
		do {
			rc = select(maxfd, &fds, NULL, NULL, NULL);
		} while (rc == -1 && errno == EINTR);

		if (rc == -1)
			throw system_error(errno, system_category());

		// Main thread requesting termination
		if (FD_ISSET(rpipe, &fds)) return false;

		do {
			rc = read(rfd, start + len, n - len);
		} while (rc < 0 && errno == EINTR);

		if (rc <= 0) {
			if (errno == EWOULDBLOCK) continue;

			return false;
		}
		len += rc;
	}
	return true;
}


Usb3003::Usb3003(const string& pathname) : UartDevice(pathname, 921600) {
}


int Usb3003::channels() const {
	return 3;
}


void Usb3003::reset() {
	flushBuffers(wfd);

	// USB-3003 dongles support hardware reset of the AMBE chip. To reset the
	// chip, we signal a break on the serial port. Upon reset, the chip will
	// send an AMBE_READY packet. Hardware reset will only work on USB-3003.
	// Other dongle variants don't seem to support it.

	if (tcsendbreak(wfd, 0) < 0)
		throw system_error(errno, system_category());

}


Usb3000::Usb3000(const string& pathname) : UartDevice(pathname, 460800) {
}


int Usb3000::channels() const {
	return 1;
}
