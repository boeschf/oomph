/*
 * ghex-org
 *
 * Copyright (c) 2014-2021, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "./context.hpp"
#include "./communicator.hpp"
#include "./channel/sender.hpp"
#include "./channel/receiver.hpp"
#include <chrono>
#ifndef NDEBUG
#include <iostream>
#endif

namespace oomph
{
communicator_impl*
context_impl::get_communicator()
{
    auto send_worker = std::make_unique<worker_type>(get(), m_db,
        (m_thread_safe ? UCS_THREAD_MODE_SERIALIZED : UCS_THREAD_MODE_SINGLE));
    auto send_worker_ptr = send_worker.get();
    if (m_thread_safe)
    {
        ucx_lock l(m_mutex);
        m_workers.push_back(std::move(send_worker));
    }
    else
    {
        m_workers.push_back(std::move(send_worker));
    }
    auto comm =
        new communicator_impl{this, m_thread_safe, m_worker.get(), send_worker_ptr, m_mutex};
    m_comms_set.insert(comm);
    return comm;
}

context_impl::~context_impl()
{
    // issue a barrier to sync all contexts
    MPI_Barrier(m_mpi_comm);

    const auto              t0 = std::chrono::system_clock::now();
    double                  elapsed = 0.0;
    static constexpr double t_timeout = 1000;

    // close endpoints while also progressing the receive worker
    std::vector<endpoint_t::close_handle> handles;
    for (auto& w_ptr : m_workers)
        for (auto& h : w_ptr->m_endpoint_handles) handles.push_back(std::move(h));

    std::vector<endpoint_t::close_handle> tmp;
    tmp.reserve(handles.size());

    while (handles.size() != 0u && elapsed < t_timeout)
    {
        for (auto& w_ptr : m_workers) ucp_worker_progress(w_ptr->m_worker);
        ucp_worker_progress(m_worker->m_worker);
        for (auto& h : handles)
        {
            if (!h.ready()) tmp.push_back(std::move(h));
        }
        handles.swap(tmp);
        tmp.clear();
        elapsed = std::chrono::duration<double, std::milli>(std::chrono::system_clock::now() - t0)
                      .count();
    }

    if (handles.size() > 0)
    {
#ifndef NDEBUG
        std::cerr << "WARNING: timeout waiting for UCX endpoint close" << std::endl;
#endif
        // free all requests for the unclosed endpoints
        for (auto& h : handles) ucp_request_free(h.m_status);
    }

    // issue another non-blocking barrier while progressing the receive worker in order to flush all
    // remaining (remote) endpoints which are connected to this receive worker
    MPI_Request req;
    int         flag;
    MPI_Ibarrier(m_mpi_comm, &req);
    while (true)
    {
        ucp_worker_progress(m_worker->m_worker);
        MPI_Test(&req, &flag, MPI_STATUS_IGNORE);
        if (flag) break;
    }

    // receive worker should not have connected to any endpoint
    assert(m_worker->m_endpoint_cache.size() == 0);

    // another MPI barrier to be sure
    MPI_Barrier(m_mpi_comm);
}

} // namespace oomph

#include "../src.cpp"

namespace oomph
{
namespace channel
{
namespace detail
{
sender::sender(communicator&& c, std::size_t s, rank_type r, tag_type t, std::size_t type_size)
: m_impl{std::move(c), s, r, t, type_size}
{
}
sender::sender(sender&& other) noexcept = default;
sender& sender::operator=(sender&& other) noexcept = default;
sender::~sender() = default;
void
sender::progress_connection()
{
    m_impl->progress_connection();
}
bool
sender::is_ready_connection() const noexcept
{
    return m_impl->is_ready_connection();
}

rma_buffer sender::request_msg()
{
    return m_impl->request_msg();
}

void sender::return_msg(rma_buffer b)
{
    assert(b);
    m_impl->return_msg(b);
}

std::size_t sender::scheduled_sends_impl() const noexcept
{
    return m_impl->scheduled_sends();
}

void
sender::progress()
{
    if (m_fcts.size())
    {
        auto d = request_msg();
        if (d) invoke(d);
    }
    m_impl->progress();
}

receiver::receiver(communicator&& c, std::size_t s, rank_type r, tag_type t, std::size_t type_size)
: m_impl{std::move(c), s, r, t, type_size}
{
}
receiver::receiver(receiver&& other) noexcept = default;
receiver& receiver::operator=(receiver&& other) noexcept = default;
receiver::~receiver() = default;
void
receiver::progress_connection()
{
    m_impl->progress_connection();
}
bool
receiver::is_ready_connection() const noexcept
{
    return m_impl->is_ready_connection();
}
rma_buffer receiver::request_msg()
{
    return m_impl->request_msg();
}

void receiver::return_msg(rma_buffer b)
{
    assert(b);
    m_impl->return_msg(b);
}

void
receiver::progress()
{
    if (m_fcts.size())
    {
        auto d = request_msg();
        if (d) invoke(d);
    }
    m_impl->progress();
}

//void signal_written(sender_impl*)
//{

//}
} // namespace detail

//void
//writer::progress()
//{
//}

} // namespace channel
} // namespace oomph
