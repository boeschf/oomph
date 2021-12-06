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

#include <oomph/communicator.hpp>
#include <oomph/util/heap_pimpl.hpp>
#include <oomph/channel/receiver_fwd.hpp>
#include <oomph/channel/detail/rma_buffer.hpp>

namespace oomph
{
namespace channel
{
namespace detail
{
struct receiver
{
    using rank_type = communicator::rank_type;
    using tag_type = communicator::tag_type;

    util::heap_pimpl<receiver_impl> m_impl;
    std::deque<std::function<void(rma_buffer)>> m_fcts;

    receiver(communicator&& c, std::size_t s, rank_type r, tag_type t, std::size_t type_size);

    receiver() noexcept = default;

    receiver(receiver&& other) noexcept;

    receiver& operator=(receiver&& other) noexcept;

    ~receiver();
    
    //void invoke(rma_buffer b)
    //{
    //    assert((bool)b);
    //    assert(m_fcts.size());
    //    m_fcts.front()(b);
    //    m_fcts.pop_front();
    //    return_msg(b);
    //}

    rma_buffer request_msg();          // progress
    void       return_msg(rma_buffer); // progress
    void       progress();
    void       progress_connection();
    bool       is_ready_connection() const noexcept;
};

} // namespace detail
} // namespace channel
} // namespace oomph
