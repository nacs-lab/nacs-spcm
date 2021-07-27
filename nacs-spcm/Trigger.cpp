//

#include "Trigger.h"
#include "SerialPort.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <system_error>
#include <stdexcept>
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
    else {
        throw std::invalid_argument(std::string("Unsupported trigger device: ") + name);
    }
}
}
