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

#ifndef _NACS_SPCM_SPCM_H
#define _NACS_SPCM_SPCM_H

#include <nacs-utils/utils.h>

#include <spcm/spcm.h>

#include <ostream>
#include <stdexcept>
#include <string>

namespace NaCs {
namespace Spcm {

struct NACS_EXPORT(spcm) Error : std::runtime_error {
    explicit Error(const std::string &what, uint32_t code,
                   uint32_t reg, int32_t val)
        : std::runtime_error(what),
        code(code),
        reg(reg),
        val(val)
        {
        }
    explicit Error(const char *what, uint32_t code,
                   uint32_t reg, int32_t val)
        : std::runtime_error(what),
        code(code),
        reg(reg),
        val(val)
        {
        }
    ~Error();

    uint32_t code;
    uint32_t reg;
    int32_t val;
};

class Spcm {
public:
    Spcm(const char *name, bool _reset=true)
        : m_hdl(spcm_hOpen(name))
    {
        if (!m_hdl) {
            throw_error();
        }
        if (_reset) {
            reset();
        }
    }
    drv_handle handle()
    {
        return m_hdl;
    }
    operator drv_handle()
    {
        return m_hdl;
    }
    ~Spcm()
    {
        spcm_vClose(m_hdl);
    }
    void check_error()
    {
        char buff[ERRORTEXTLEN];
        uint32_t reg;
        int32_t val;
        auto code = spcm_dwGetErrorInfo_i32(m_hdl, &reg, &val, buff);
        if (likely(!code))
            return;
        throw_error(buff, code, reg, val);
    }
    [[noreturn]] void throw_error();
    [[noreturn]] static void throw_error(const char *msg, uint32_t code,
                                         uint32_t reg, int32_t val);
    void clear_error()
    {
        // The document doesn't mention that the message field could be NULL.
        // However, it is used in the examples
        // and the disassembed code also shows a NULL check on this argument.
        spcm_dwGetErrorInfo_i32(m_hdl, nullptr, nullptr, nullptr);
    }

    template<typename T>
    uint32_t set_param(int32_t name, T value)
    {
        if (sizeof(T) >= 8) {
            return spcm_dwSetParam_i64(m_hdl, name, (int64)value);
        }
        else {
            return spcm_dwSetParam_i32(m_hdl, name, (int32)value);
        }
    }
    template<typename T>
    uint32_t get_param(int32_t name, T *p)
    {
        if (sizeof(T) >= 8) {
            int64 buff;
            auto err = spcm_dwGetParam_i64(m_hdl, name, &buff);
            *p = T(buff);
            return err;
        }
        else {
            int32 buff;
            auto err = spcm_dwGetParam_i32(m_hdl, name, &buff);
            *p = T(buff);
            return err;
        }
    }
    uint32_t def_transfer(uint32_t type, uint32_t dir, uint32_t notify_size, void *buff,
                          uint64_t offset, uint64_t size) // Size in byte
    {
        return spcm_dwDefTransfer_i64(m_hdl, type, dir, notify_size, buff, offset, size);
    }
    uint32_t invalidate_buf(uint32_t type)
    {
        return spcm_dwInvalidateBuf(m_hdl, type);
    }

    // Register wrappers
    int32_t card_type()
    {
        int32_t res;
        get_param(SPC_PCITYP, &res);
        return res;
    }
    std::pair<uint16_t,uint16_t> pci_version()
    {
        return get_param_16x2(SPC_PCIVERSION);
    }
    std::pair<uint8_t,uint8_t> basepcb_version()
    {
        return get_param_8x2(SPC_BASEPCBVERSION);
    }
    std::pair<uint16_t,uint16_t> pcimodule_version()
    {
        return get_param_16x2(SPC_PCIMODULEVERSION);
    }
    std::pair<uint8_t,uint8_t> modulepcb_version()
    {
        return get_param_8x2(SPC_MODULEPCBVERSION);
    }
    std::pair<uint16_t,uint16_t> pciext_version()
    {
        return get_param_16x2(SPC_PCIEXTVERSION);
    }
    std::pair<uint8_t,uint8_t> extpcb_version()
    {
        return get_param_8x2(SPC_EXTPCBVERSION);
    }
    int32_t pxi_hwslotno()
    {
        int32_t res;
        if (get_param(SPC_PXIHWSLOTNO, &res)) {
            clear_error();
            return -1;
        }
        return res;
    }

    std::pair<uint16_t,uint16_t> fw_ctrl_version()
    {
        return get_param_16x2(SPCM_FW_CTRL);
    }
    std::pair<uint16_t,uint16_t> fw_ctrl_golden_version()
    {
        return get_param_16x2(SPCM_FW_CTRL_GOLDEN);
    }
    std::pair<uint16_t,uint16_t> fw_ctrl_active_version()
    {
        auto ver = get_param_16x2(SPCM_FW_CTRL_ACTIVE).second;
        return {uint16_t(ver & 0xfff), uint16_t(ver >> 12)};
    }
    std::pair<uint16_t,uint16_t> fw_clock_version()
    {
        return get_param_16x2(SPCM_FW_CLOCK);
    }
    std::pair<uint16_t,uint16_t> fw_config_version()
    {
        return get_param_16x2(SPCM_FW_CONFIG);
    }
    std::pair<uint16_t,uint16_t> fw_modulea_version()
    {
        return get_param_16x2(SPCM_FW_MODULEA);
    }
    std::pair<uint16_t,uint16_t> fw_moduleb_version()
    {
        return get_param_16x2(SPCM_FW_MODULEB);
    }
    std::pair<uint16_t,uint16_t> fw_modextra_version()
    {
        return get_param_16x2(SPCM_FW_MODEXTRA);
    }
    std::pair<uint16_t,uint16_t> fw_power_version()
    {
        return get_param_16x2(SPCM_FW_POWER);
    }

    std::pair<uint16_t,uint16_t> product_date()
    {
        return get_param_16x2(SPC_PCIDATE);
    }
    std::pair<uint16_t,uint16_t> calib_date()
    {
        return get_param_16x2(SPC_CALIBDATE);
    }
    uint32_t serial_no()
    {
        uint32_t res;
        get_param(SPC_PCISERIALNO, &res);
        return res;
    }
    uint64_t max_sample_rate()
    {
        uint64_t res;
        get_param(SPC_PCISAMPLERATE, &res);
        return res;
    }
    uint64_t mem_size()
    {
        uint64_t res;
        get_param(SPC_PCIMEMSIZE, &res);
        return res;
    }
    uint32_t features()
    {
        uint32_t res;
        get_param(SPC_PCIFEATURES, &res);
        return res;
    }
    uint32_t ext_features()
    {
        uint32_t res;
        get_param(SPC_PCIEXTFEATURES, &res);
        return res;
    }
    uint32_t cmd(int32_t cmd)
    {
        uint32_t res;
        res = set_param(SPC_M2CMD, cmd);
        return res;
    }
    void reset()
    {
        cmd(M2CMD_CARD_RESET);
    }
    void write_setup()
    {
        cmd(M2CMD_CARD_WRITESETUP);
    }
    void force_trigger()
    {
        cmd(M2CMD_CARD_FORCETRIGGER);
    }
    void ch_enable(int32_t chns)
    {
        set_param(SPC_CHENABLE, chns);
        check_error();
    }
    int32_t ch_enable()
    {
        int32_t chns;
        get_param(SPC_CHENABLE, &chns);
        return chns;
    }
    int32_t ch_count()
    {
        int32_t nchn;
        get_param(SPC_CHCOUNT, &nchn);
        return nchn;
    }
    void enable_out(unsigned chn, bool enable)
    {
        if (chn >= 4)
            throw_error("enable_out: channel out of bound", ERR_REG, 0, 0);
        set_param(int32_t(SPC_ENABLEOUT0 + 100 * chn), enable);
    }
    bool out_enabled(unsigned chn)
    {
        if (chn >= 4)
            throw_error("enable_out: channel out of bound", ERR_REG, 0, 0);
        int32_t enable;
        get_param(int32_t(SPC_ENABLEOUT0 + 100 * chn), &enable);
        return enable != 0;
    }
    uint32_t amp(unsigned chn)
    {
        if (chn >= 4)
            throw_error("enable_out: channel out of bound", ERR_REG, 0, 0);
        int32_t amp;
        get_param(int32_t(SPC_AMP0 + 100 * chn), &amp);
        return amp;
    }
    void set_amp(unsigned chn, uint32_t amp)
    {
        if (chn >= 4)
            throw_error("enable_out: channel out of bound", ERR_REG, 0, 0);
        if (!set_param(int32_t(SPC_AMP0 + 100 * chn), amp)) {
            check_error();
        }
    }

    uint32_t x0_availmodes()
    {
        uint32_t res;
        get_param(SPCM_X0_AVAILMODES, &res);
        return res;
    }
    uint32_t x1_availmodes()
    {
        uint32_t res;
        get_param(SPCM_X1_AVAILMODES, &res);
        return res;
    }
    uint32_t x2_availmodes()
    {
        uint32_t res;
        get_param(SPCM_X2_AVAILMODES, &res);
        return res;
    }

    uint32_t x0_mode()
    {
        uint32_t res;
        get_param(SPCM_X0_MODE, &res);
        return res;
    }
    uint32_t x1_mode()
    {
        uint32_t res;
        get_param(SPCM_X1_MODE, &res);
        return res;
    }
    uint32_t x2_mode()
    {
        uint32_t res;
        get_param(SPCM_X2_MODE, &res);
        return res;
    }

    void x0_mode(uint32_t mode)
    {
        set_param(SPCM_X0_MODE, mode);
        check_error();
    }
    void x1_mode(uint32_t mode)
    {
        set_param(SPCM_X1_MODE, mode);
        check_error();
    }
    void x2_mode(uint32_t mode)
    {
        set_param(SPCM_X2_MODE, mode);
        check_error();
    }
    void dump(std::ostream &stm) noexcept;

private:
    std::pair<uint16_t,uint16_t> get_param_16x2(int32_t name)
    {
        uint32_t res;
        get_param(name, &res);
        return {uint16_t(res), uint16_t(res >> 16)};
    }
    std::pair<uint8_t,uint8_t> get_param_8x2(int32_t name)
    {
        uint32_t res;
        get_param(name, &res);
        return {uint8_t(res), uint8_t(res >> 8)};
    }
    drv_handle m_hdl;
};

}
}

#endif
