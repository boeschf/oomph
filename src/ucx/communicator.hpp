/*
 * ghex-org
 *
 * Copyright (c) 2014-2021, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#pragma once

#include <boost/lockfree/queue.hpp>
#include <oomph/context.hpp>
#include "../communicator_base.hpp"
#include "../device_guard.hpp"
#include "./request_data.hpp"
#include "./context.hpp"

namespace oomph
{
#define OOMPH_UCX_TAG_BITS             32
#define OOMPH_UCX_RANK_BITS            32
#define OOMPH_UCX_ANY_SOURCE_MASK      0x0000000000000000ul
#define OOMPH_UCX_SPECIFIC_SOURCE_MASK 0x00000000fffffffful
#define OOMPH_UCX_TAG_MASK             0xffffffff00000000ul

class communicator_impl : public communicator_base<communicator_impl>
{
  public:
    using worker_type = worker_t;
    using rank_type = communicator::rank_type;
    using tag_type = communicator::tag_type;
    template<typename T>
    using lockfree_queue = boost::lockfree::queue<T, boost::lockfree::fixed_sized<false>,
        boost::lockfree::allocator<std::allocator<void>>>;

    using recv_req_queue_type = lockfree_queue<detail::request_state*>;

  public:
    context_impl*                       m_context;
    bool const                          m_thread_safe;
    worker_type*                        m_recv_worker;
    worker_type*                        m_send_worker;
    ucx_mutex&                          m_mutex;
    recv_req_queue_type                 m_recv_req_queue;
    recv_req_queue_type                 m_cancel_recv_req_queue;
    std::vector<detail::request_state*> m_cancel_recv_req_vec;

  public:
    communicator_impl(context_impl* ctxt, bool thread_safe, worker_type* recv_worker,
        worker_type* send_worker, ucx_mutex& mtx)
    : communicator_base(ctxt)
    , m_context(ctxt)
    , m_thread_safe{thread_safe}
    , m_recv_worker{recv_worker}
    , m_send_worker{send_worker}
    , m_mutex{mtx}
    , m_recv_req_queue(128)
    , m_cancel_recv_req_queue(128)
    {
    }

    ~communicator_impl()
    {
        // schedule all endpoints for closing
        for (auto& kvp : m_send_worker->m_endpoint_cache)
        {
            m_send_worker->m_endpoint_handles.push_back(kvp.second.close());
            m_send_worker->m_endpoint_handles.back().progress();
        }
    }

    auto& get_heap() noexcept { return m_context->get_heap(); }

    void progress()
    {
        while (ucp_worker_progress(m_send_worker->get())) {}
        if (m_thread_safe)
        {
#ifdef OOMPH_UCX_USE_SPIN_LOCK
            // this is really important for large-scale multithreading: check if still is
            sched_yield();
#endif
            {
                // progress recv worker in locked region
                //ucx_lock lock(m_mutex);
                //while (ucp_worker_progress(m_recv_worker->get())) {}
                for (unsigned int i=0; i<10; ++i)
                {
                    if (m_mutex.try_lock())
                    {
                        auto p = ucp_worker_progress(m_recv_worker->get());
                        m_mutex.unlock();
                        if (!p) break;
                    }
                }
            }
        }
        else
        {
            for (unsigned int i=0; i<10; ++i)
            {
                if (m_mutex.try_lock())
                {
                    auto p = ucp_worker_progress(m_recv_worker->get());
                    m_mutex.unlock();
                    if (!p) break;
                }
            }
            //while (ucp_worker_progress(m_recv_worker->get())) {}
        }
        // work through ready recv callbacks, which were pushed to the queue by other threads
        // (including this thread)
        if (m_thread_safe)
            m_recv_req_queue.consume_all(
                [](detail::request_state* req)
                {
                    auto ptr = std::move(req->m_self_ptr);
                    req->invoke_cb();
                });
        m_context->m_recv_req_queue.consume_all(
            [](detail::shared_request_state* req)
            {
                auto ptr = std::move(req->m_self_ptr);
                req->invoke_cb();
            });
    }

    send_request send(context_impl::heap_type::pointer const& ptr, std::size_t size, rank_type dst,
        tag_type tag, util::unique_function<void(rank_type, tag_type)>&& cb, std::size_t* scheduled)
    {
        const auto& ep = m_send_worker->connect(dst);
        const auto  stag =
            ((std::uint_fast64_t)tag << OOMPH_UCX_TAG_BITS) | (std::uint_fast64_t)(rank());

        ucs_status_ptr_t ret;
        {
            // device is set according to message memory: needed?
            const_device_guard dg(ptr);

            ret = ucp_tag_send_nb(ep.get(),         // destination
                dg.data(),                          // buffer
                size,                               // buffer size
                ucp_dt_make_contig(1),              // data type
                stag,                               // tag
                &communicator_impl::send_callback); // callback function pointer
        }

        if (reinterpret_cast<std::uintptr_t>(ret) == UCS_OK)
        {
            // send operation is completed immediately
            // call the callback
            cb(dst, tag);
            return {};
            // request is freed by ucx internally
        }
        else if (!UCS_PTR_IS_ERR(ret))
        {
            // send operation was scheduled
            // allocate request_state
            auto s = m_req_state_factory.make(m_context, this, scheduled, dst, tag, std::move(cb),
                ret, m_mutex);
            s->m_self_ptr = s;
            // attach necessary data to the request
            request_data::construct(ret, s.get());
            return {std::move(s)};
        }
        else
        {
            // an error occurred
            throw std::runtime_error("oomph: ucx error - send operation failed");
        }
    }

    recv_request recv(context_impl::heap_type::pointer& ptr, std::size_t size, rank_type src,
        tag_type tag, util::unique_function<void(rank_type, tag_type)>&& cb, std::size_t* scheduled)
    {
        const auto rtag =
            (communicator::any_source == src)
                ? ((std::uint_fast64_t)tag << OOMPH_UCX_TAG_BITS)
                : ((std::uint_fast64_t)tag << OOMPH_UCX_TAG_BITS) | (std::uint_fast64_t)(src);

        const auto rtag_mask = (communicator::any_source == src)
                                   ? (OOMPH_UCX_TAG_MASK | OOMPH_UCX_ANY_SOURCE_MASK)
                                   : (OOMPH_UCX_TAG_MASK | OOMPH_UCX_SPECIFIC_SOURCE_MASK);

        if (m_thread_safe) m_mutex.lock();
        ucs_status_ptr_t ret;
        {
            // device is set according to message memory: needed?
            device_guard dg(ptr);

            ret = ucp_tag_recv_nb(m_recv_worker->get(), // worker
                dg.data(),                              // buffer
                size,                                   // buffer size
                ucp_dt_make_contig(1),                  // data type
                rtag,                                   // tag
                rtag_mask,                              // tag mask
                &communicator_impl::recv_callback);     // callback function pointer
        }

        if (!UCS_PTR_IS_ERR(ret))
        {
            if (UCS_INPROGRESS != ucp_request_check_status(ret))
            {
                // early completed
                ucp_request_free(ret);
                if (m_thread_safe) m_mutex.unlock();
                cb(src, tag);
                return {};
            }
            else
            {
                // recv operation was scheduled
                // allocate request_state
                auto s = m_req_state_factory.make(m_context, this, scheduled, src, tag,
                    std::move(cb), ret, m_mutex);
                s->m_self_ptr = s;
                // attach necessary data to the request
                request_data::construct(ret, s.get());
                if (m_thread_safe) m_mutex.unlock();
                return {std::move(s)};
            }
        }
        else
        {
            // an error occurred
            throw std::runtime_error("oomph: ucx error - recv operation failed");
        }
    }

    shared_recv_request shared_recv(context_impl::heap_type::pointer& ptr, std::size_t size,
        rank_type src, tag_type tag, util::unique_function<void(rank_type, tag_type)>&& cb,
        std::atomic<std::size_t>* scheduled)
    {
        const auto rtag =
            (communicator::any_source == src)
                ? ((std::uint_fast64_t)tag << OOMPH_UCX_TAG_BITS)
                : ((std::uint_fast64_t)tag << OOMPH_UCX_TAG_BITS) | (std::uint_fast64_t)(src);

        const auto rtag_mask = (communicator::any_source == src)
                                   ? (OOMPH_UCX_TAG_MASK | OOMPH_UCX_ANY_SOURCE_MASK)
                                   : (OOMPH_UCX_TAG_MASK | OOMPH_UCX_SPECIFIC_SOURCE_MASK);

        if (m_thread_safe) m_mutex.lock();
        ucs_status_ptr_t ret;
        {
            // device is set according to message memory: needed?
            device_guard dg(ptr);

            ret = ucp_tag_recv_nb(m_recv_worker->get(), // worker
                dg.data(),                              // buffer
                size,                                   // buffer size
                ucp_dt_make_contig(1),                  // data type
                rtag,                                   // tag
                rtag_mask,                              // tag mask
                &communicator_impl::recv_callback);     // callback function pointer
        }

        if (!UCS_PTR_IS_ERR(ret))
        {
            if (UCS_INPROGRESS != ucp_request_check_status(ret))
            {
                // early completed
                ucp_request_free(ret);
                if (m_thread_safe) m_mutex.unlock();
                cb(src, tag);
                return {};
            }
            else
            {
                // recv operation was scheduled
                // allocate shared request_state
                auto s = std::make_shared<detail::shared_request_state>(m_context, this, scheduled,
                    src, tag, std::move(cb), ret, m_mutex);
                s->m_self_ptr = s;
                // attach necessary data to the request
                request_data::construct(ret, s.get());
                if (m_thread_safe) m_mutex.unlock();
                return {std::move(s)};
            }
        }
        else
        {
            // an error occurred
            throw std::runtime_error("oomph: ucx error - recv operation failed");
        }
    }

    void enqueue_recv(detail::request_state* d)
    {
        while (!m_recv_req_queue.push(d)) {}
    }

    void enqueue_cancel_recv(detail::request_state* d)
    {
        while (!m_cancel_recv_req_queue.push(d)) {}
    }

    inline static void send_callback(void* ucx_req, ucs_status_t status)
    {
        auto& req_data = *request_data::get(ucx_req);
        if (status == UCS_OK)
        {
            // invoke callback
            if (req_data.m_req)
            {
                auto req = req_data.m_req;
                auto ptr = std::move(req->m_self_ptr);
                req->invoke_cb();
            }
            else
            {
                auto req = req_data.m_shared_req;
                auto ptr = std::move(req->m_self_ptr);
                req->invoke_cb();
            }
        }
        // else: cancelled - do nothing - cancel for sends does not exist

        // destroy request
        req_data.destroy();
        ucp_request_free(ucx_req);
    }

    // this callback is called within a locked region
    inline static void recv_callback(void* ucx_req, ucs_status_t status,
        ucp_tag_recv_info_t* /*info*/)
    {
        auto& req_data = *request_data::get(ucx_req);
        if (status == UCS_OK)
        {
            // return if early completion
            if (req_data.empty()) return;

            if (req_data.m_req)
            {
                // normal recv
                auto req = req_data.m_req;
                if (req->m_ctxt->thread_safe())
                {
                    // multi-threaded case
                    // free request here
                    req_data.destroy();
                    ucp_request_free(ucx_req);
                    // enqueue request on the issuing communicator
                    // this guarantees that only the communicator on which the receive was issued
                    // will invoke the callback
                    req->m_comm->enqueue_recv(req);
                }
                else
                {
                    // single-threaded case
                    // free request here
                    req_data.destroy();
                    ucp_request_free(ucx_req);
                    // call the callback directly from here
                    auto ptr = std::move(req->m_self_ptr);
                    req->invoke_cb();
                }
            }
            else
            {
                // shared recv
                auto req = req_data.m_shared_req;
                // free request here
                req_data.destroy();
                ucp_request_free(ucx_req);
                // enqueue request on the context
                req->m_ctxt->enqueue_recv(req);
            }
        }
        else if (status == UCS_ERR_CANCELED)
        {
            // receive was cancelled
            if (req_data.m_req) req_data.m_req->m_comm->enqueue_cancel_recv(req_data.m_req);
            else
                req_data.m_shared_req->m_ctxt->enqueue_cancel_recv(req_data.m_shared_req);
        }
        else
        {
            // an error occurred
            throw std::runtime_error("oomph: ucx error - recv message truncated");
        }
    }

    // Note: at this time, send requests cannot be canceled in UCX (1.7.0rc1)
    // https://github.com/openucx/ucx/issues/1162
    //bool cancel_recv_cb(recv_request const& req)
    bool cancel_recv(detail::request_state* s)
    {
        if (m_thread_safe) m_mutex.lock();
        ucp_request_cancel(m_recv_worker->get(), s->m_ucx_ptr);
        //if (m_thread_safe) m_mutex.unlock();
        // The ucx callback will still be executed after the cancel. However, the status argument
        // will indicate whether the cancel was successful.
        // Progress the receive worker in order to execute the ucx callback
        //if (m_thread_safe) m_mutex.lock();
        while (ucp_worker_progress(m_recv_worker->get())) {}
        if (m_thread_safe) m_mutex.unlock();
        // check whether the cancelled callback was enqueued by consuming all queued cancelled
        // callbacks and putting them in a temporary vector
        bool found = false;
        m_cancel_recv_req_vec.clear();
        m_cancel_recv_req_queue.consume_all(
            [this, s, &found](detail::request_state* r)
            {
                if (r == s) found = true;
                else
                    m_cancel_recv_req_vec.push_back(r);
            });
        // re-enqueue all callbacks which were not identical with the current callback
        for (auto x : m_cancel_recv_req_vec)
            while (!m_cancel_recv_req_queue.push(x)) {}

        // delete callback here if it was actually cancelled
        if (found)
        {
            auto ptr = std::move(s->m_self_ptr);
            s->set_canceled();
            void* ucx_req = s->m_ucx_ptr;
            // destroy request
            request_data::get(ucx_req)->destroy();
            if (m_thread_safe) m_mutex.lock();
            ucp_request_free(ucx_req);
            if (m_thread_safe) m_mutex.unlock();
        }
        return found;
    }
};

} // namespace oomph
