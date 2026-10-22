//

#include "Trigger.h"
#include "SerialPort.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <system_error>
#include <stdexcept>
//CHAT FIX
/*#include <termios.h>

static void make_raw(int fd) {
    struct termios tio{};
    if (tcgetattr(fd, &tio) == 0) {
        cfmakeraw(&tio);                      // raw: no NL→CR, no echo, no processing
        tio.c_cflag |= CLOCAL | CREAD;        // enable receiver
        tio.c_iflag &= ~(IXON | IXOFF | IXANY); // no software flow control
        tio.c_cc[VMIN]  = 0;                  // non-blocking read
        tio.c_cc[VTIME] = 0;
        tcsetattr(fd, TCSANOW, &tio);
        tcflush(fd, TCIFLUSH);                // drop any stale bytes
    }
    }*/

//END OF FIX
namespace Spcm {
NACS_EXPORT() Trigger::Trigger(const char *name)
{
    if (strcmp(name, "/dev/null") == 0) {
        // disabled
        fd = -1;
    }
    else if (strcmp(name, "/dev/tty") == 0 ||
             strcmp(name, "/dev/stdin") == 0) {
        // Terminal input
        fd = open(name, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0)
            throw std::system_error(errno, std::generic_category(),
                                    "Error opening trigger stream");
        // CHAT FIX
        //make_raw(fd);
        // END FIX
        cb = [] (int fd) {
            char buff[16];
            int res = read(fd, buff, 16);
            if (res <= 0)
                return std::make_pair(0, 0);
            return std::make_pair(res, 0);
        };
    }
    else if (strncmp(name, "/dev/tty", strlen("/dev/tty")) == 0) {
        Serial::Options options;
        options.baud_rate = B115200;
        options.char_size = CS8;
        fd = Serial::open(name, options);
        if (fd < 0)
            throw std::system_error(-fd, std::generic_category(), "Error opening trigger stream");
        cb = [] (int fd) {
            unsigned char buff[8];
            uint32_t nchunks = 0;
            int res = read(fd, buff, 8);
            if (res <= 0)
                return std::make_pair(nchunks,(int64_t) 0);
            int64_t val;
            memcpy(&val, buff, 8);
            nchunks++;
            while (read(fd,buff, 8) > 0)
            {
                nchunks++;
            }
            //for (int i = 0; i < 8; i++) {
            //    printf("%X ", buff[i]);
            //}
            //printf("trigger val: %lli\n", val);
            return std::make_pair(nchunks, val);
            };
    }
        //CHAT FIX
        /*make_raw(fd);

                 
        cb = [] (int fd) {
    // Persist across invocations of the callback
                 static uint8_t spill[8];    // leftover bytes from the previous call
                 static int     have = 0;
                 uint8_t buf[1024];
                 int     r    = ::read(fd, buf, sizeof(buf));
                 if (r <= 0) {
                     return std::make_pair(0u, (int64_t)0);
                 }

    // First, combine spill + new into a virtual stream
                 uint8_t stream[sizeof(spill) + sizeof(buf)];
                 int     slen = 0;
                 if (have) { memcpy(stream, spill, have); slen += have; }
                 memcpy(stream + slen, buf, r); slen += r;
    // How many complete 8-byte frames do we have?
                 int frames   = slen / 8;
                 int leftover = slen % 8;
                 int64_t last = 0;
                 if (frames > 0) {
        // Take the NEWEST complete frame (the last 8 bytes in the complete portion)
                     const uint8_t* p = stream + (frames - 1) * 8;
        // Assemble explicitly as little-endian to avoid any UB
                     uint64_t v = 0;
                     for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
                     memcpy(&last, &v, 8);
                 }

    // Save the trailing leftovers for next time
                 have = leftover;
                 if (leftover) memcpy(spill, stream + (frames * 8), leftover);
                 return std::make_pair((uint32_t)frames, last);
                 };
                 }*/

    else {
        throw std::invalid_argument(std::string("Unsupported trigger device: ") + name);
    }
}
}
