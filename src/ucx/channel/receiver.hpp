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

#include <hwmalloc/numa.hpp>
#include <oomph/channel/detail/receiver.hpp>
#include "../communicator.hpp"
#include "../context.hpp"
#include "./sender_status.hpp"
#include <stdexcept>

namespace oomph
{
namespace channel
{
class receiver_impl
{
    using rank_type = communicator::rank_type;
    using tag_type = communicator::tag_type;
    using heap_type = rma_context::heap_type;
    using pointer = heap_type::pointer;
    using handle_type = pointer::handle_type;

    communicator           m_comm;
    worker_t*              m_worker;
    address_t              m_local_address;
    context_impl*          m_context;
    heap_type&             m_heap;
    rank_type              m_remote_rank;
    tag_type               m_tag;
    std::size_t            m_size = 0;
    std::size_t            m_type_size;
    pointer                m_buffer;
    pointer                m_status_buffer;
    void*                  m_status_buffer_ptr;
    handle_type            m_status_buffer_handle;
    volatile unsigned int* m_flag;
    std::size_t            m_rkey_size;
    rma_params             m_rma_params;
    message_buffer<char>   m_status_msg;
    message_buffer<char>   m_done_msg;
    //message_buffer<char>        m_put_msg;
    pointer                     m_put_msg;
    rma_request                 m_put_req;
    oomph::recv_request                m_request;
    oomph::recv_request                m_done_request;
    std::unique_ptr<endpoint_t> m_endpoint;
    ucp_rkey_h                  m_remote_status_key;
    std::uintptr_t              m_remote_status_addr;
    ucp_rkey_h                  m_remote_buffer_key;
    std::uintptr_t              m_remote_buffer_addr;
    bool                        m_is_connected = false;
    rma_request m_get_req;

    enum class recv_state
    {
        ready,
        wait,
        transfer,
        read
    };

    recv_state m_recv_state = recv_state::ready;

    static std::size_t round_up_size(std::size_t s)
    {
        return ((s + sizeof(std::size_t) - 1) / sizeof(std::size_t)) * sizeof(std::size_t);
    }

  public:
    receiver_impl(communicator&& c, std::size_t s, rank_type r, tag_type t, std::size_t type_size)
    : m_comm{std::move(c)}
    , m_worker{m_comm.m_impl->m_send_worker}
    , m_local_address{m_worker->address()}
    , m_context{m_comm.m_impl->m_context}
    , m_heap{m_context->m_rma_context.m_heap}
    , m_remote_rank{r}
    , m_tag{t}
    , m_size{s}
    , m_type_size{type_size}
    , m_buffer{m_heap.allocate(m_size * m_type_size, hwmalloc::numa().local_node())}
    , m_status_buffer{m_heap.allocate(sizeof(unsigned int), hwmalloc::numa().local_node())}
    , m_status_buffer_ptr{m_status_buffer.get()}
    , m_status_buffer_handle{m_status_buffer.handle()}
    , m_flag{(unsigned int volatile*)m_status_buffer.get()}
    , m_rkey_size{m_status_buffer_handle.m_rkey_size}
    , m_rma_params{m_rkey_size, m_local_address.size()}
    , m_status_msg{m_comm.make_buffer<char>(m_rma_params.sender_status_msg_size())}
    , m_done_msg{m_comm.make_buffer<char>(1)}
    //, m_put_msg{m_comm.make_buffer<char>(m_rma_params.sender_status_size())}
    , m_put_msg{m_heap.allocate(m_rma_params.sender_status_size(), hwmalloc::numa().local_node())}
    {
        *m_flag = 0;
        // receive status message from sender
        m_request = m_comm.recv(m_status_msg, r, t);
    }

    ~receiver_impl()
    {
        ucp_rkey_destroy(m_remote_status_key);
        ucp_rkey_destroy(m_remote_buffer_key);
        m_worker->m_endpoint_handles.push_back(m_endpoint->close());
        ucp_worker_progress(m_worker->m_worker.get());
        m_heap.free(m_buffer);
        m_heap.free(m_put_msg);
        m_heap.free(m_status_buffer);
    }

    ucs_status_t worker_flush(ucp_worker_h worker)
    {
        void* request = ucp_worker_flush_nb(worker, 0, &empty_send_callback);
        if (request == NULL) { return UCS_OK; }
        else if (UCS_PTR_IS_ERR(request))
        {
            return UCS_PTR_STATUS(request);
        }
        else
        {
            ucs_status_t status;
            do {
                ucp_worker_progress(worker);
                status = ucp_request_check_status(request);
            } while (status == UCS_INPROGRESS);
            ucp_request_release(request);
            return status;
        }
    }

    ucs_status_t put(ucp_ep_h ep, const void* buffer, size_t length, uint64_t remote_addr,
        ucp_rkey_h rkey)
    {
        void* request = ucp_put_nb(ep, buffer, length, remote_addr, rkey, empty_send_callback);
        if (request == NULL) { return UCS_OK; }
        else if (UCS_PTR_IS_ERR(request))
        {
            return UCS_PTR_STATUS(request);
        }
        else
        {
            ucs_status_t status;
            do {
                ucp_worker_progress(m_worker->m_worker.get());
                status = ucp_request_check_status(request);
            } while (status == UCS_INPROGRESS);
            ucp_request_release(request);
            return status;
        }
    }

    void progress_connection()
    {
        if (m_is_connected) return;
        if (!m_request.is_ready())
        {
            if (m_request.test())
            {
                // unpack status message
                sender_status_msg msg(m_rma_params, m_status_msg.data());

                // create endpoint
                m_endpoint = std::make_unique<endpoint_t>(m_remote_rank, m_worker->m_worker.get(),
                    msg.get_worker_address());

                // create status rkey and address
                OOMPH_CHECK_UCX_RESULT(ucp_ep_rkey_unpack(m_endpoint->m_ep, msg.get_status_rkey(),
                    &m_remote_status_key));
                m_remote_status_addr = msg.get_status_addr();

                // create buffer rkey and address
                OOMPH_CHECK_UCX_RESULT(ucp_ep_rkey_unpack(m_endpoint->m_ep, msg.get_buffer_rkey(),
                    &m_remote_buffer_key));
                m_remote_buffer_addr = msg.get_buffer_addr();

                // compose put message
                //sender_status status{m_rma_params, m_put_msg.data()};
                sender_status status{m_rma_params, m_put_msg.get()};
                status.set_worker_address(m_local_address)
                    .set_status_rkey(m_status_buffer_handle.m_rkey)
                    .set_status_addr(m_status_buffer.get());
                status.flag() = 2;

                //std::cout << "put msg created" << std::endl;

                // put message to sender
                m_put_req =
                    rma_request(ucp_put_nb(m_endpoint->m_ep,
                                    //m_put_msg.data(),
                                    m_put_msg.get(), m_rma_params.sender_status_size(),
                                    m_remote_status_addr, m_remote_status_key, empty_send_callback),
                        m_worker->m_worker.get());
                //put(m_endpoint->m_ep,
                //    //m_put_msg.data(),
                //    m_put_msg.get(), m_rma_params.sender_status_size(), m_remote_status_addr,
                //    m_remote_status_key);

                //std::cout << "put finished" << std::endl;

                ////if (ret == UCS_OK) std::cout << "put completed immediately" << std::endl;
                //worker_flush(m_worker->m_worker.get());
                //ucp_worker_progress(m_worker->m_worker.get());
            }
        }
        else if (!m_put_req.is_ready())
        {
            //std::cout << "testing put req"<< std::endl;
            //if (m_put_req.test())
            //{
            //    std::cout << "put finished" << std::endl;
            //}
            m_put_req.test();
        }
        else
        {
            ucp_worker_progress(m_worker->m_worker.get());
            if (*m_flag == 1)
            {
                //std::cout << "recv: all finished" << std::endl;
                //ucp_worker_progress(m_worker->m_worker.get());
                //ucp_worker_progress(m_worker->m_worker.get());
                //ucp_worker_progress(m_worker->m_worker.get());
                //    if (vflag())
                //    {
                //m_is_connected = true;
                //    }
                *m_flag = 0;
                m_done_request = m_comm.recv(m_done_msg, m_remote_rank, m_tag);
            }
            else if (m_done_request.test())
            {
                m_is_connected = true;
            }
        }
    }

    bool is_ready_connection() const noexcept { return m_is_connected; }

    //struct recv_request
    //{

    //    void test()
    //    {
    //    }
    //};

    //[[nodiscard]] recv_request recv()
    //{
    //    recv_request req{};
    //    req.test();
    //    return req;
    //}

    detail::rma_buffer request_msg()
    {
        if (m_recv_state == recv_state::ready)
        {
            m_recv_state = recv_state::wait;
            return {};
        }
        if (m_recv_state == recv_state::read)
        //if (m_recv_state != recv_state::ready) return {};
        //m_recv_state = send_state::wait;
        //return {m_buffer.get(),m_size,0}; 
    }
    
    void return_msg(detail::rma_buffer)
    {
        m_send_state = send_state::transfer;
        // set remote flag
        m_remote_flag = 1;
                m_put_req =
                    rma_request(ucp_put_nb(m_endpoint->m_ep, &m_remote_flag, sizeof(unsigned int),
                                    m_remote_status_addr, m_remote_status_key, empty_send_callback),
                        m_worker->m_worker.get());
            ucp_worker_progress(m_worker->m_worker.get());
    }
    
    std::size_t scheduled_sends() const noexcept
    {
        return m_send_state == send_state::ready? 0 : 1;
    }

    void progress()
    {
        if (m_send_state != send_state::transfer) return;
        if (m_status.flag() == 1)
        {
            m_status.flag() = 0;
            m_send_state = send_state::ready;
        }
        else
        {
            if (m_put_req.test())
            ucp_worker_progress(m_worker->m_worker.get());
        }
    }
};

} // namespace channel
} // namespace oomph
