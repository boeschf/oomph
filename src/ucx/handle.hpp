/*
 * GridTools
 *
 * Copyright (c) 2014-2021, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "./error.hpp"

namespace oomph
{
struct handle
{
    void*       m_ptr;
    std::size_t m_size;
};

} // namespace oomph
