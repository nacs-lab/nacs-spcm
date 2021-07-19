//

#ifndef _NACS_SPCM_SERIALPORT_H
#define _NACS_SPCM_SERIALPORT_H

#include <termios.h>
#include <stdint.h>

namespace Spcm {

namespace Serial {

enum class Parity : uint8_t {
    Even,
    Odd,
    None,
};

enum class FlowControl : uint8_t {
    Hard,
    Soft,
    None,
};

struct Options {
    bool in{true};
    bool out{true};
    bool cloexec{true};
    bool nonblock{true};
    speed_t baud_rate{B576000};
    tcflag_t char_size{CS8};
    Parity parity{Parity::None};
    short num_stop_bits{1};
    FlowControl flow_ctrl{FlowControl::None};
};

int open(const char *name, const Options &options=Options{});

}

}

#endif
