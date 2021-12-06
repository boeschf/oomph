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
#include <oomph/channel/detail/sender.hpp>
#include <oomph/channel/rma_buffer.hpp>
#include <iterator>

namespace oomph
{
namespace channel
{
template<typename T>
class sender
{
  public:
    using rank_type = communicator::rank_type;
    using tag_type = communicator::tag_type;
    using value_type = T;

  private:
    friend class oomph::future_traits<sender<T>>;
    friend class oomph::context;

  private:
    detail::sender m_sender;

  private:
    sender(communicator&& c, std::size_t s, rank_type r, tag_type t)
    : m_sender{std::move(c), s, r, t, sizeof(T)}
    {
    }

  public:
    sender() noexcept = default;

    sender(sender&& other) noexcept = default;

    sender& operator=(sender&& other) noexcept = default;

  public:
    detail::sender& impl() noexcept { return m_sender; }

  public:
    template<typename F>
    void write(F&& f)
    {
        m_sender.m_fcts.push_back(
            [g = std::forward<F>(f)](detail::rma_buffer b)
            {
                auto& b_T = reinterpret_cast<rma_buffer<T>&>(b);
                g(b_T);
            });
    }

    void progress()
    {
        //if (m_sender.m_fcts.size())
        //{
        //    auto d = m_sender.request_msg();
        //    if (d) m_sender.invoke(d);
        //}
        //else if (m_sender.scheduled_sends_impl())
        //{
        //    m
        //}
        m_sender.progress();
    }

    std::size_t scheduled_sends() const noexcept { return m_sender.scheduled_sends(); }

  private:

    //detail::rma_buffer request_msg() { return m_sender.request_msg(); }
    //void invoke(detail::rma_buffer b) { m_sender.invoke(b); }

  private:
    friend class oomph::future_traits<sender>;

    void progress_future() { m_sender.progress_connection(); }

    bool is_ready_future() const noexcept { return m_sender.is_ready_connection(); }
};

//template<typename T>
//writer
//make_writer(sender<T>& s)
//{
//    sender_impl* s_ptr = s.impl().m_impl.get();
//    return {&s_ptr, 1};
//    //return detail::make_writer(
//    //    //[g = std::forward<F>(f)](detail::rma_buffer* first, std::size_t count)
//    //    //{
//    //    //    auto first_T = reinterpret_cast<rma_buffer<T>*>(first);
//    //    //    g(first_T, count);
//    //    //},
//    //    &s_ptr, 1);
//}

//template<typename F, typename T, typename... Ts>
//writer
//write(F&& f, sender<T>& s0, sender<T>& s1, sender<Ts>&... s)
//{
//    sender_impl* s_arr[2 + sizeof...(Ts)] = {s0.impl().m_impl.get(), s1.impl().m_impl.get(),
//        s.impl().m_impl.get()...};
//    return detail::write(
//        [g = std::forward<F>(f)](detail::rma_buffer* first, std::size_t count)
//        {
//            auto first_T = reinterpret_cast<rma_buffer<T>*>(first);
//            g(first_T, count);
//        },
//        s_arr, 1);
//}
//
//template<typename F, typename Iterator>
//writer
//write(F&& f, Iterator first, Iterator last)
//{
//    using T = typename std::iterator_traits<Iterator>::value_type::value_type;
//    std::vector<sender_impl*> s_vec(std::distance(first, last));
//    for (std::size_t i = 0; i < s_vec.size(); ++i)
//    {
//        s_vec[i] = first->impl().m_impl.get();
//        std::advance(first, 1);
//    }
//    return detail::write(
//        [g = std::forward<F>(f)](detail::rma_buffer* first, std::size_t count)
//        {
//            auto first_T = reinterpret_cast<rma_buffer<T>*>(first);
//            g(first_T, count);
//        },
//        s_vec.data(), 1);
//}
//
//template<typename F, typename Range>
//writer
//write(F&& f, Range&& r)
//{
//    return write(r.begin(), r.end());
//}

} // namespace channel
} // namespace oomph
