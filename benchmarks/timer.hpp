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

#include "./accumulator.hpp"
#include <chrono>

namespace oomph
{
/** @brief timer with built-in statistics */
class timer : public accumulator
{
  private: // member types
    using base = accumulator;
    using clock_type = std::chrono::high_resolution_clock;
    using time_point = typename clock_type::time_point;
    using duration = typename clock_type::duration;

  private: // members
    time_point m_time_point = clock_type::now();
    double m_period_ms =
        (double)((clock_type::period::num * 1000000)) / clock_type::period::den; // micro seconds

    double to_ms(duration const& d) const noexcept
    {
        return d.count() * m_period_ms;
        //return std::chrono::duration_cast<D>(d).count();
    }

  public: // ctors
    timer() = default;
    timer(const base& b)
    : base(b)
    {
    }
    timer(base&& b)
    : base(std::move(b))
    {
    }
    timer(const timer&) noexcept = default;
    timer(timer&&) noexcept = default;
    timer& operator=(const timer&) noexcept = default;
    timer& operator=(timer&&) noexcept = default;

  public: // time functions
    /** @brief start timings */
    inline void tic() noexcept { m_time_point = clock_type::now(); }
    /** @brief stop timings */
    inline double stoc() noexcept { return to_ms(clock_type::now() - m_time_point); }

    /** @brief stop timings */
    inline double toc() noexcept
    {
        const auto t = to_ms(clock_type::now() - m_time_point);
        this->     operator()(t);
        return t;
    }

    /** @brief stop timings, verbose: print measured time */
    inline void vtoc() noexcept
    {
        const auto t = to_ms(clock_type::now() - m_time_point);
        std::cout << "time:      " << t / 1000000 << "s\n";
    }

    /** @brief stop timings, verbose: print measured time and bandwidth */
    inline void vtoc(const char* header, long bytes) noexcept
    {
        const auto t = to_ms(clock_type::now() - m_time_point);
        std::cout << header << " MB/s:      " << bytes / t << "\n";
    }

    /** @brief stop and start another timing period */
    inline void toc_tic() noexcept
    {
        auto  t2 = clock_type::now();
        this->operator()(to_ms(t2 - m_time_point));
        m_time_point = t2;
    }
};

/** @brief all-reduce timers over the MPI group defined by the communicator
          * @param t timer local to each rank
          * @param comm MPI communicator
          * @return combined timer incorporating statistics over all timings */
timer
reduce(const timer& t, MPI_Comm comm)
{
    return reduce(static_cast<accumulator>(t), comm);
}

} // namespace oomph
