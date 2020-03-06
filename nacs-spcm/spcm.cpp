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
    int typ = card_type();
    int nchn = 4;
    const char *card_name = nullptr;
    switch (typ) {
#define def_case(TYP, name, chn)                \
    case TYP:                                   \
        card_name = name;                       \
        nchn = chn;                             \
        break
        def_case(TYP_M4I6620_X8, "M4i.6620-x8", 1);
        def_case(TYP_M4I6630_X8, "M4i.6630-x8", 1);
        def_case(TYP_M4X6620_X4, "M4x.6620-x4", 1);
        def_case(TYP_M4X6630_X4, "M4x.6630-x4", 1);
        def_case(TYP_M4I6621_X8, "M4i.6621-x8", 2);
        def_case(TYP_M4I6631_X8, "M4i.6631-x8", 2);
        def_case(TYP_M4X6621_X4, "M4x.6621-x4", 2);
        def_case(TYP_M4X6631_X4, "M4x.6631-x4", 2);
        def_case(TYP_M4I6622_X8, "M4i.6622-x8", 4);
        def_case(TYP_M4X6622_X4, "M4x.6622-x4", 4);
#undef def_case
    default:
        nchn = 4;
        break;
    }
    if (card_name) {
        stm << "Card type: " << card_name << std::endl;
    }
    else {
        stm << "Card type: 0x" << std::hex << typ << std::dec << std::endl;
    }
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

    stm << "Firmware versions:" << std::endl;
    ver = fw_ctrl_version();
    stm << "  Main control FPGA version: " << ver.first
        << ", type: " << ver.second << std::endl;
    ver = fw_ctrl_golden_version();
    stm << "  Main control FPGA golden version: " << ver.first
        << ", type: " << ver.second << std::endl;
    ver = fw_clock_version();
    stm << "  Clock distribution version: " << ver.first
        << ", type: " << ver.second << std::endl;
    ver = fw_config_version();
    stm << "  Configuration controller version: " << ver.first
        << ", type: " << ver.second << std::endl;
    ver = fw_modulea_version();
    stm << "  Frontend module A version: " << ver.first
        << ", type: " << ver.second << std::endl;
    ver = fw_moduleb_version();
    stm << "  Frontend module B version: " << ver.first
        << ", type: " << ver.second << std::endl;
    ver = fw_modextra_version();
    stm << "  Extension module version: " << ver.first
        << ", type: " << ver.second << std::endl;
    ver = fw_power_version();
    stm << "  Power controller version: " << ver.first
        << ", type: " << ver.second << std::endl;

    auto date = product_date();
    stm << "Product date: " << date.first << " / week " << date.second << std::endl;
    date = calib_date();
    stm << "Calibration date: " << date.first << " / week " << date.second << std::endl;
    stm << "Serial no: " << serial_no() << std::endl;
    stm << "Max sample rate: " << max_sample_rate() << std::endl;
    stm << "Memory size: " << mem_size() << std::endl;
    stm << "Features: 0x" << std::hex << features() << std::dec << std::endl;
    stm << "Ext features: 0x" << std::hex << ext_features() << std::dec << std::endl;

    auto enable_mask = ch_enable();
    stm << "Channel enabled mask: 0x" << std::hex << enable_mask << std::dec << std::endl;
    stm << "Enabled channel count: " << ch_count() << std::endl;
    for (int i = 0; i < nchn; i++) {
        bool enabled = enable_mask & (1 << i);
        stm << "  Channel [" << i << "]: " << (enabled ? "enabled" : "disabled")
            << ", output " <<  (out_enabled(i) ? "enabled" : "disabled")
            << ", amp: " << amp(i) << std::endl;
    }

    stm << "X0 available modes: 0x" << std::hex << x0_availmodes()
        << ", X0 mode: 0x" << x0_mode() << std::dec << std::endl;
    stm << "X1 available modes: 0x" << std::hex << x1_availmodes()
        << ", X1 mode: 0x" << x1_mode() << std::dec << std::endl;
    stm << "X2 available modes: 0x" << std::hex << x2_availmodes()
        << ", X2 mode: 0x" << x2_mode() << std::dec << std::endl;

    try {
        check_error();
    }
    catch (const NaCs::Spcm::Error &err) {
        stm << "Error: " << err.what() << std::endl;
    }
}

}
}
