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
#include <oomph/channel/sender_fwd.hpp>
#include <oomph/channel/detail/rma_buffer.hpp>
#include <vector>
#include <deque>
#include <functional>

namespace oomph
{
namespace channel
{
//namespace detail
//{
//struct rma_buffer
//{
//    void*       m_ptr = nullptr;
//    std::size_t m_count = 0;
//    std::size_t m_index = 0;
//    operator bool() const noexcept { return (bool)m_ptr; }
//};
//
////void signal_written(sender_impl*);
//} // namespace detail
//
//template<typename T>
//struct rma_buffer
//{
//    detail::rma_buffer m_buffer;
//    operator bool() const noexcept { return (bool)m_buffer; }
//    std::size_t size() const noexcept { return m_buffer.m_count(); }
//    T* data() noexcept { return (T*)(m_buffer.m_ptr); }
//    T const * data() const noexcept { return (T const *)(m_buffer.m_ptr); }
//};

//struct writer
//{
//    std::vector<sender_impl*>         m_senders;
//    std::vector<detail::rma_buffer>   m_buffers;
//    std::deque<std::function<void()>> m_fcts;
//
//    //template<typename F>
//    writer(sender_impl** first, std::size_t count)
//    : m_senders{first, first + count}
//    , m_buffers{count}
//    {
//    }
//
//    //template<typename F>
//    //writer(F&& f, sender_impl** first, std::size_t count)
//    //: m_senders{first, first + count}
//    //, m_buffers{count}
//    //, m_fct{[g = std::forward<F>(f), s_ptr = m_senders.data(), b_ptr = m_buffers.data(), count]()
//    //      {
//    //          g(b_ptr, count);
//    //          for (std::size_t i = 0; i < count; ++i) detail::signal_written(s_ptr[i]);
//    //      }}
//    //{
//    //}
//
//    template<typename F>
//    void write(F&& f)
//    {
//        m_fcts.push_back(
//            [g = std::forward<F>(f), s_ptr = m_senders.data(), b_ptr = m_buffers.data(),
//                count = m_senders.size()]() { g(b_ptr, count); });
//    }
//
//    void progress();
//    //for (std::size_t i = 0; i < count; ++i) detail::signal_written(s_ptr[i]);
//};

namespace detail
{
struct sender
{
    using rank_type = communicator::rank_type;
    using tag_type = communicator::tag_type;

    util::heap_pimpl<sender_impl>               m_impl;
    std::deque<std::function<void(rma_buffer)>> m_fcts;

    sender(communicator&& c, std::size_t s, rank_type r, tag_type t, std::size_t type_size);

    sender() noexcept = default;

    sender(sender&& other) noexcept;

    sender& operator=(sender&& other) noexcept;

    ~sender();

    //void invoke(rma_buffer b)
    //{
    //    assert((bool)b);
    //    assert(m_fcts.size());
    //    m_fcts.front()(b);
    //    m_fcts.pop_front();
    //    return_msg(b);
    //}

    rma_buffer  request_msg();          // progress
    void        return_msg(rma_buffer); // progress
    void        progress();
    std::size_t scheduled_sends() const noexcept { return scheduled_sends_impl() + m_fcts.size(); }
    std::size_t scheduled_sends_impl() const noexcept;
    void        progress_connection();
    bool        is_ready_connection() const noexcept;
};

//writer
//make_writer(sender_impl** first, std::size_t count)
//{
//    return {std::forward<F>(f), first, count};
//}

} // namespace detail
} // namespace channel
} // namespace oomph
