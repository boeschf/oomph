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

#include <cstddef>

namespace oomph
{
namespace channel
{
namespace detail
{
struct rma_buffer
{
    void*       m_ptr = nullptr;
    std::size_t m_count = 0;
    std::size_t m_index = 0;
                operator bool() const noexcept { return (bool)m_ptr; }
};
} // namespace detail
} // namespace channel
} // namespace oomph
