/*
 * ghex-org
 *
 * Copyright (c) 2014-2021, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <oomph/channel/detail/rma_buffer.hpp>

namespace oomph
{
namespace channel
{
template<typename T>
struct rma_buffer
{
    detail::rma_buffer m_buffer;
                       operator bool() const noexcept { return (bool)m_buffer; }
    std::size_t        size() const noexcept { return m_buffer.m_count(); }
    T*                 data() noexcept { return (T*)(m_buffer.m_ptr); }
    T const*           data() const noexcept { return (T const*)(m_buffer.m_ptr); }
};

} // namespace channel
} // namespace oomph
