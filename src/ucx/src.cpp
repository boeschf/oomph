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
#include <chrono>
#ifndef NDEBUG
#include <iostream>
#endif

namespace oomph
{
communicator_impl*
context_impl::get_communicator()
{
#ifdef OOMPH_UCX_USE_MULTIPLE_ENDPOINTS
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
#else
    auto comm =
        new communicator_impl{this, m_thread_safe, m_worker.get(), m_worker.get(), m_mutex};
#endif
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

#ifdef OOMPH_UCX_USE_MULTIPLE_ENDPOINTS
    // close endpoints while also progressing the receive worker
    std::vector<endpoint_t::close_handle> handles;
    for (auto& w_ptr : m_workers)
        for (auto& h : w_ptr->m_endpoint_handles) handles.push_back(std::move(h));

    std::vector<endpoint_t::close_handle> tmp;
    tmp.reserve(handles.size());

    while (handles.size() != 0u && elapsed < t_timeout)
    {
        for (auto& h : handles)
        {
            ucp_worker_progress(m_worker->m_worker);
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
#endif

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
#ifdef OOMPH_UCX_USE_MULTIPLE_ENDPOINTS
    assert(m_worker->m_endpoint_cache.size() == 0);
#endif

    // another MPI barrier to be sure
    MPI_Barrier(m_mpi_comm);
}

const char *context_impl::get_transport_option(const std::string &opt) {
    if (opt == "name") {
        return "ucx";
    }
    else {
        return "unspecified";
    }
}

} // namespace oomph

#include "../src.cpp"
