//

// This is a thin wrapper around the serial port API
// Most of the code is tweaked from libserial. We don't use it because it doesn't support polling.

#include "SerialPort.h"
//#include "Utils.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/serial.h>
#include <nacs-utils/utils.h>

#include <cstring>
#include <iostream>

namespace Spcm {
namespace Serial {

NACS_EXPORT() int open(const char *name, const Options &options)
{
    int flags;
    if (options.in) {
        flags = options.out ? O_RDWR : O_RDONLY;
    }
    else if (options.out) {
        flags = O_WRONLY;
    }
    else {
        return -EINVAL;
    }
    if (options.cloexec)
        flags |= O_CLOEXEC;
    // Use non-blocking mode while configuring the serial port.
    // We'll set it back later if needed.
    flags |= O_NONBLOCK;
    flags |= O_NOCTTY;
    int fd = ::open(name, flags);
    if (fd == -1)
        return -errno;
    int res = tcflush(fd, TCIOFLUSH);
    if (res == -1)
        goto error;

    struct termios tio;
    if (tcgetattr(fd, &tio) == -1)
        goto error;

    tio.c_iflag = IGNBRK;
    tio.c_oflag = 0;
    tio.c_cflag = B19200 | CS8 | CLOCAL | CREAD;
    tio.c_lflag = 0;
    // From libserial
    // termios.c_line is not a standard element of the termios structure (as
    // per the Single Unix Specification 2. This is only present under Linux.
#ifdef __linux__
    tio.c_line = '\0';
#endif
    std::memset(&tio.c_cc, 0, sizeof(tio.c_cc));
    tio.c_cc[VTIME] = 0;
    tio.c_cc[VMIN] = 1;

    switch(options.char_size) {
    case CS8:
    case CS5:
    case CS6:
    case CS7:
        // From libserial
        // Set the character size to the specified value. If the character
        // size is not 8 then it is also important to set ISTRIP. Setting
        // ISTRIP causes all but the 7 low-order bits to be set to
        // zero. Otherwise they are set to unspecified values and may
        // cause problems. At the same time, we should clear the ISTRIP
        // flag when the character size is 8 otherwise the MSB will always
        // be set to zero (ISTRIP does not check the character size
        // setting; it just sets every bit above the low 7 bits to zero).
        //
        if (options.char_size == CS8) {
            tio.c_iflag &= ~ISTRIP; // clear the ISTRIP flag.
        } else {
            tio.c_iflag |= ISTRIP;  // set the ISTRIP flag.
        }

        tio.c_cflag &= ~CSIZE;     // clear all the CSIZE bits.
        tio.c_cflag |= options.char_size;  // set the character size.
        break;
    default:
        errno = EINVAL;
        goto error;
    }
    switch (options.num_stop_bits) {
    case 1:
        tio.c_cflag &= ~CSTOPB;
        break;
    case 2:
        tio.c_cflag |= CSTOPB;
        break;
    default:
        errno = EINVAL;
        goto error;
    }
    switch (options.parity) {
    case Parity::Even:
        tio.c_cflag |= PARENB;
        tio.c_cflag &= ~PARODD;
        break;
    case Parity::Odd:
        tio.c_cflag |= PARENB;
        tio.c_cflag |= PARODD;
        break;
    case Parity::None:
        tio.c_cflag &= ~PARENB;
        break;
    default:
        errno = EINVAL;
        goto error;
    }
    switch (options.flow_ctrl) {
    case FlowControl::Hard:
        tio.c_iflag &= ~ (IXON|IXOFF);
        tio.c_cflag |= CRTSCTS;
        tio.c_cc[VSTART] = _POSIX_VDISABLE;
        tio.c_cc[VSTOP] = _POSIX_VDISABLE;
        break;
    case FlowControl::Soft:
        tio.c_iflag |= IXON|IXOFF;
        tio.c_cflag &= ~CRTSCTS;
        tio.c_cc[VSTART] = 0x11; // (021) ^q
        tio.c_cc[VSTOP] = 0x13; // (023) ^s
        break;
    case FlowControl::None:
        tio.c_iflag &= ~(IXON|IXOFF);
        tio.c_cflag &= ~CRTSCTS;
        break;
    default:
        errno = EINVAL;
        goto error;
    }
    if (cfsetispeed(&tio, options.baud_rate) == -1 || cfsetospeed(&tio, options.baud_rate) == -1)
        goto error;

    if (tcsetattr(fd, TCSANOW, &tio) == -1)
        goto error;

    if (!options.nonblock) {
        auto fl = fcntl(fd, F_GETFL, 0);
        if (fcntl(fd, F_SETFL, fl & ~O_NONBLOCK) == -1) {
            goto error;
        }
    }

    // {
    //     serial_struct serial;
    //     int res = ioctl(fd, TIOCGSERIAL, &serial);
    //     serial.flags |= ASYNC_LOW_LATENCY; // (0x2000)
    //     res = ioctl(fd, TIOCSSERIAL, &serial);
    //     (void)res;
    // }

    return fd;
error: {
        int res = -errno;
        close(fd);
        return res;
    }
}

}
}
