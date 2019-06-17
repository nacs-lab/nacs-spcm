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
    Spcm(const char *name)
        : m_hdl(spcm_hOpen(name))
    {
        if (!m_hdl) {
            throw_error();
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

private:
    drv_handle m_hdl;
};

}
}

#endif
