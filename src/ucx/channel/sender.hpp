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
#include <oomph/channel/detail/sender.hpp>
#include "../communicator.hpp"
#include "../context.hpp"
#include "./sender_status.hpp"

namespace oomph
{
namespace channel
{
class sender_impl
{
    using rank_type = communicator::rank_type;
    using tag_type = communicator::tag_type;
    using heap_type = rma_context::heap_type; //context_impl::heap_type;
    using pointer = heap_type::pointer;
    using handle_type = pointer::handle_type;

    communicator                m_comm;
    worker_t*                   m_worker;
    address_t                   m_local_address;
    context_impl*               m_context;
    heap_type&                  m_heap;
    rank_type                   m_remote_rank;
    tag_type                    m_tag;
    std::size_t                 m_size = 0;
    std::size_t                 m_type_size;
    pointer                     m_buffer;
    handle_type                 m_buffer_handle;
    std::size_t                 m_rkey_size;
    rma_params                  m_rma_params;
    pointer                     m_status_buffer;
    void*                       m_status_buffer_ptr;
    handle_type                 m_status_buffer_handle;
    sender_status               m_status;
    message_buffer<char>        m_status_msg;
    message_buffer<char>        m_done_msg;
    oomph::send_request         m_request;
    oomph::send_request         m_done_request;
    bool                        m_is_connected = false;
    std::unique_ptr<endpoint_t> m_endpoint;
    ucp_rkey_h                  m_remote_status_key;
    std::uintptr_t              m_remote_status_addr;
    unsigned int                m_remote_flag = 0;
    rma_request                 m_put_req;

    enum class send_state
    {
        ready,
        fill,
        transfer
    };

    send_state m_send_state = send_state::ready;

  public:
    sender_impl(communicator&& c, std::size_t s, rank_type r, tag_type t, std::size_t type_size)
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
    , m_buffer_handle{m_buffer.handle()}
    , m_rkey_size{m_buffer_handle.m_rkey_size}
    , m_rma_params{m_rkey_size, m_local_address.size()}
    , m_status_buffer{m_heap.allocate(m_rma_params.sender_status_size(),
          hwmalloc::numa().local_node())}
    , m_status_buffer_ptr{m_status_buffer.get()}
    , m_status_buffer_handle{m_status_buffer.handle()}
    , m_status{m_rma_params, m_status_buffer.get()}
    , m_status_msg{m_comm.make_buffer<char>(m_rma_params.sender_status_msg_size())}
    , m_done_msg{m_comm.make_buffer<char>(1)}
    {
        m_status.flag() = 0;

        sender_status_msg msg(m_rma_params, m_status_msg.data());

        msg.set_worker_address(m_local_address)
            .set_status_rkey(m_status_buffer_handle.m_rkey)
            .set_status_addr(m_status_buffer.get())
            .set_buffer_rkey(m_buffer_handle.m_rkey)
            .set_buffer_addr(m_buffer.get());

        m_request = m_comm.send(m_status_msg, r, t);
    }

    ~sender_impl()
    {
        ucp_rkey_destroy(m_remote_status_key);
        m_worker->m_endpoint_handles.push_back(m_endpoint->close());
        ucp_worker_progress(m_worker->m_worker.get());
        m_heap.free(m_buffer);
        m_heap.free(m_status_buffer);
    }

    void progress_connection()
    {
        if (m_is_connected) return;
        if (!m_request.is_ready())
        {
            m_request.test();
            //if (m_request.test()) m_request.wait();
        }
        else
        {
            //if (vflag())
            //{
            //}
            if (m_status.flag() == 0) { ucp_worker_progress(m_worker->m_worker.get()); }
            else if (m_status.flag() == 2)
            {
                ucp_worker_progress(m_worker->m_worker.get());
                // unpack receiver worker, rkey and address

                // create endpoint
                m_endpoint = std::make_unique<endpoint_t>(m_remote_rank, m_worker->m_worker.get(),
                    m_status.get_worker_address());

                // create status rkey and address
                OOMPH_CHECK_UCX_RESULT(ucp_ep_rkey_unpack(m_endpoint->m_ep,
                    m_status.get_status_rkey(), &m_remote_status_key));
                m_remote_status_addr = m_status.get_status_addr();

                // put remote flag
                m_remote_flag = 1;
                m_put_req =
                    rma_request(ucp_put_nb(m_endpoint->m_ep, &m_remote_flag, sizeof(unsigned int),
                                    m_remote_status_addr, m_remote_status_key, empty_send_callback),
                        m_worker->m_worker.get());

                //m_is_connected = true;
                //m_status.flag() = 0;
                m_status.flag() = 1;
            }
            else if (!m_put_req.is_ready())
            {
                if (m_put_req.test())
                {
                    m_done_request = m_comm.send(m_done_msg, m_remote_rank, m_tag);
                }
            }
            //else if (!(m_status.flag()==1))
            //{
            //    ucp_worker_progress(m_worker->m_worker.get());
            //}
            else
            {
                if (m_done_request.test())
                {
                    m_status.flag() = 0;
                    m_is_connected = true;
                }
            }
        }
    }
    bool is_ready_connection() const noexcept { return m_is_connected; }

    //struct send_request
    //{
    //    unsigned int volatile* m_flag = nullptr;
    //    rma_request            m_put_req;
    //    bool                   m_is_ready = false;

    //    send_request() = default;

    //    send_request(unsigned int volatile* flag, rma_request&& req)
    //    : m_flag{flag}
    //    , m_put_req{std::move(req)}
    //    {
    //    }

    //    send_request(send_request&& other) noexcept
    //    : m_flag{std::exchange(other.m_flag, nullptr)}
    //    , m_put_req{std::move(other.m_put_req)}
    //    , m_is_ready{std::exchange(other.m_is_ready, false)}
    //    {
    //    }

    //    send_request& operator=(send_request&& other) noexcept
    //    {
    //        assert(!m_flag);
    //        m_flag = std::exchange(other.m_flag, nullptr);
    //        m_put_req = std::move(other.m_put_req);
    //        m_is_ready = std::exchange(other.m_is_ready, false);
    //        return *this;
    //    }

    //    bool is_ready() const noexcept { return m_is_ready; }

    //    bool test() noexcept
    //    {
    //        if (m_is_ready) return true;
    //        if (!m_flag) return false;
    //        if (m_put_req.test())
    //        {
    //            if (*m_flag == 1)
    //            {
    //                m_is_ready = true;
    //                *m_flag = 0;
    //                m_flag = nullptr;
    //                return true;
    //            }
    //            else
    //                return false;
    //        }
    //        else
    //            return false;
    //    }
    //};

    //[[nodiscard]] send_request send()
    //{
    //    assert(m_status.flag() == 0);
    //    // set remote flag
    //    m_remote_flag = 1;
    //    return {m_status.m_flag_ptr,
    //        rma_request(ucp_put_nb(m_endpoint->m_ep, &m_remote_flag, sizeof(unsigned int),
    //                        m_remote_status_addr, m_remote_status_key, empty_send_callback),
    //            m_worker->m_worker.get())};
    //}
    //
    
    detail::rma_buffer request_msg()
    {
        if (m_send_state != send_state::ready) return {};
        m_send_state = send_state::fill;
        return {m_buffer.get(),m_size,0}; 
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
