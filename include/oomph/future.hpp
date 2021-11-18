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

#include <memory>

namespace oomph
{
template<typename T>
struct future_traits
{
    static void progress(T& t) { t.progress_future(); }
    static bool is_ready(T const& t) noexcept { return t.is_ready_future(); }
};

template<typename T>
class future
{
  private:
    using traits = future_traits<T>;

  private:
    std::unique_ptr<T> m;

  public:
    future() noexcept = default;

    future(T&& t) noexcept
    : m{std::make_unique<T>(std::move(t))}
    {
    }

    future(future&&) noexcept = default;

    future& operator=(future&&) noexcept = default;

  public:
    void progress()
    {
        if (!m) return;
        traits::progress(*m);
    }

    bool is_ready() const noexcept
    {
        if (!m) return true;
        return traits::is_ready(*m);
    }

    bool test()
    {
        if (!m) return true;
        if (is_ready()) return true;
        progress();
        return is_ready();
    }

    T get()
    {
        while (!test()) {}
        return std::move(*m.release());
    }
};

} // namespace oomph
