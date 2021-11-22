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

#include <cstdint>

namespace oomph
{
class thread_id
{
    using id_type = std::uintptr_t const;

  private:
    id_type* const m;

  public:
    thread_id();
    ~thread_id();
    thread_id(thread_id const&) = delete;
    thread_id(thread_id&) = delete;
    thread_id& operator=(thread_id const&) = delete;
    thread_id& operator=(thread_id&&) = delete;

  public:
    friend bool operator==(thread_id const& a, thread_id const& b) noexcept { return (a.m == b.m); }
    friend bool operator!=(thread_id const& a, thread_id const& b) noexcept { return (a.m != b.m); }

    operator std::uintptr_t() const& noexcept { return *m; }
};

thread_id const& tid();
} // namespace oomph
