//

#include "Trigger.h"

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
                return 0;
            return res;
        };
    }
    else {
        throw std::invalid_argument(std::string("Unsupported trigger device: ") + name);
    }
}
}
