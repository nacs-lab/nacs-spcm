/*************************************************************************
 *   Copyright (c) 2019 - 2019 Yichao Yu <yyc1992@gmail.com>             *
 *                                                                       *
 *   This library is free software; you can redistribute it and/or       *
 *   modify it under the terms of the GNU Lesser General Public          *
 *   License as published by the Free Software Foundation; either        *
 *   version 3.0 of the License, or (at your option) any later version.  *
 *                                                                       *
 *   This library is distributed in the hope that it will be useful,     *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of      *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    *
 *   Lesser General Public License for more details.                     *
 *                                                                       *
 *   You should have received a copy of the GNU Lesser General Public    *
 *   License along with this library. If not,                            *
 *   see <http://www.gnu.org/licenses/>.                                 *
 *************************************************************************/

#include "spcm.h"

namespace NaCs {
namespace Spcm {

Error::~Error()
{
}

NACS_EXPORT() void Spcm::throw_error()
{
    char buff[ERRORTEXTLEN];
    uint32_t reg;
    int32_t val;
    auto code = spcm_dwGetErrorInfo_i32(m_hdl, &reg, &val, buff);
    throw_error(buff, code, reg, val);
}

NACS_EXPORT() void Spcm::throw_error(const char *msg, uint32_t code,
                                     uint32_t reg, int32_t val)
{
    throw Error(msg, code, reg, val);
}

NACS_EXPORT() void Spcm::dump(std::ostream &stm) noexcept
{
    stm << "Card type: " << card_type() << std::endl;
    std::pair<int,int> ver;
    ver = pci_version();
    stm << "Base card version: firmware: " << ver.first
        << ", hardware: " << ver.second << std::endl;
    ver = basepcb_version();
    stm << "Base card PCB version: " << ver.first << "." << ver.second << std::endl;
    ver = pcimodule_version();
    stm << "Module version: firmware: " << ver.first
        << ", hardware: " << ver.second << std::endl;
    ver = modulepcb_version();
    stm << "Module PCB version: " << ver.first << "." << ver.second << std::endl;
    ver = pciext_version();
    stm << "Ext version: firmware: " << ver.first
        << ", hardware: " << ver.second << std::endl;
    ver = extpcb_version();
    stm << "Ext PCB version: " << ver.first << "." << ver.second << std::endl;
    auto pxi_slotno = pxi_hwslotno();
    if (pxi_slotno > 0)
        stm << "PXI hardware slot no: " << pxi_slotno << std::endl;
    else
        stm << "PXI hardware slot no: N/A" << std::endl;
    auto date = product_date();
    stm << "Product date: " << date.first << " / week " << date.second << std::endl;
    date = calib_date();
    stm << "Calibration date: " << date.first << " / week " << date.second << std::endl;
    stm << "Serial no: " << serial_no() << std::endl;
    stm << "Max sample rate: " << max_sample_rate() << std::endl;
    stm << "Memory size: " << mem_size() << std::endl;
    stm << "Features: 0x" << std::hex << features() << std::dec << std::endl;
    stm << "Ext features: 0x" << std::hex << ext_features() << std::dec << std::endl;

    int nchn = ch_count();
    stm << "Channel count: " << nchn << std::endl;
    stm << "Channel enabled mask: " << std::hex << ch_enable() << std::dec << std::endl;
    for (int i = 0; i < nchn; i++) {
        if (i > 4) {
            stm << "Invalid channel count (> 4)" << std::endl;
            break;
        }
        stm << "Channel [" << i << "]: output " <<  (out_enabled(i) ? "enabled" : "disabled")
            << ", amp: " << amp(i) << std::endl;
    }

    stm << "X0 available modes: " << std::hex << x0_availmodes()
        << ", X0 mode: " << x0_mode() << std::dec << std::endl;
    stm << "X1 available modes: " << std::hex << x1_availmodes()
        << ", X1 mode: " << x1_mode() << std::dec << std::endl;
    stm << "X2 available modes: " << std::hex << x2_availmodes()
        << ", X2 mode: " << x2_mode() << std::dec << std::endl;

    try {
        check_error();
    }
    catch (const NaCs::Spcm::Error &err) {
        stm << "Error: " << err.what() << std::endl;
    }
}

}
}
