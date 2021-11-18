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

#include <oomph/future.hpp>
#include <oomph/channel/detail/receiver.hpp>

namespace oomph
{
namespace channel
{
template<typename T>
class receiver
{
  public:
    using rank_type = communicator::rank_type;
    using tag_type = communicator::tag_type;

  private:
    friend class oomph::future_traits<receiver<T>>;
    friend class oomph::context;

  private:
    detail::receiver m_receiver;

  private:
    receiver(communicator&& c, std::size_t s, rank_type r, tag_type t)
    : m_receiver{std::move(c), s, r, t, sizeof(T)}
    {
    }

  public:
    receiver() noexcept = default;

    receiver(receiver&& other) noexcept = default;

    receiver& operator=(receiver&& other) noexcept = default;

  public:
    T*       data() noexcept { return nullptr; }
    T const* data() const noexcept { return nullptr; }

    //std::size_t size() const noexcept { return m_size; }

  private:
    friend class oomph::future_traits<receiver>;

    void progress_future() { m_receiver.progress_connection(); }

    bool is_ready_future() const noexcept { return m_receiver.is_ready_connection(); }
};

} // namespace channel
} // namespace oomph

