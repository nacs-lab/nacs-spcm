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

#include <nacs-spcm/spcm.h>
#include <nacs-utils/log.h>

#include <inttypes.h>

using namespace NaCs;

int main(int argc, char **argv)
{
    if (argc < 2) {
        Log::error("Missing device name.\n");
        return 1;
    }
    NaCs::Spcm::Spcm hdl(argv[1]);
    Log::log("Card type: %d\n", hdl.card_type());
    std::pair<int,int> ver;
    ver = hdl.pci_version();
    Log::log("Base card version: firmware: %d, hardware: %d\n", ver.first, ver.second);
    ver = hdl.basepcb_version();
    Log::log("Base card PCB version: %d.%d\n", ver.second, ver.first);
    ver = hdl.pcimodule_version();
    Log::log("Module version: firmware: %d, hardware: %d\n", ver.first, ver.second);
    ver = hdl.modulepcb_version();
    Log::log("Module PCB version: %d.%d\n", ver.second, ver.first);
    ver = hdl.pciext_version();
    Log::log("Ext version: firmware: %d, hardware: %d\n", ver.first, ver.second);
    ver = hdl.extpcb_version();
    Log::log("Ext PCB version: %d.%d\n", ver.second, ver.first);
    Log::log("PXI hardware slot no: %d\n", hdl.pxi_hwslotno());
    auto date = hdl.product_date();
    Log::log("Product date: %d / week %d\n", date.first, date.second);
    date = hdl.calib_date();
    Log::log("Calibration date: %d / week %d\n", date.first, date.second);
    Log::log("Serial no: %d\n", hdl.serial_no());
    Log::log("Max sample rate: %" PRId64 "\n", hdl.max_sample_rate());
    Log::log("Memory size: %" PRId64 "\n", hdl.mem_size());
    Log::log("Features: %x\n", hdl.features());
    Log::log("Ext features: %x\n", hdl.ext_features());

    Log::log("X0 available modes: %x, X0 mode: %x\n", hdl.x0_availmodes(), hdl.x0_mode());
    Log::log("X1 available modes: %x, X1 mode: %x\n", hdl.x1_availmodes(), hdl.x1_mode());
    Log::log("X2 available modes: %x, X2 mode: %x\n", hdl.x2_availmodes(), hdl.x2_mode());

    try {
        hdl.check_error();
    }
    catch (const NaCs::Spcm::Error &err) {
        Log::log("Error: %s", err.what());
    }

    return 0;
}
