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

}
}
