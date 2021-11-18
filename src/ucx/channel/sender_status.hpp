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

#include <cstddef> // std::size_t
#include <cstdint> // std::uintptr_t
#include <cstring> // std::memcpy
#include <array>
#include "../error.hpp"
#include "../address.hpp"

namespace oomph
{
namespace channel
{
struct rma_params
{
    std::size_t m_rkey_size = 0u;
    std::size_t m_worker_address_size = 0u;

    std::size_t sender_status_msg_size() const noexcept
    {
        return m_worker_address_size + 2 * (m_rkey_size + sizeof(std::uintptr_t));
    }

    std::size_t sender_status_size() const noexcept
    {
        std::size_t s = m_worker_address_size + m_rkey_size + sizeof(std::uintptr_t);
        std::size_t s_p = ((s + 63) / 64) * 64;
        return s_p + sizeof(unsigned int);
    }

    std::size_t sender_status_flag_offset() const noexcept
    {
        return sender_status_size() - sizeof(unsigned int);
    }
};

struct sender_status
{
    // +----------------+-------------+----------------+------+
    // | worker address | status rkey | status address | flag |
    // +----------------+-------------+----------------+------+

    std::array<void volatile*, 4> m_slot;
    std::array<std::size_t, 4>    m_size;
    unsigned int volatile*        m_flag_ptr;

    sender_status(rma_params const& p, void* ptr) noexcept
    {
        m_slot[0] = ptr;
        m_size[0] = p.m_worker_address_size;

        m_slot[1] = (char*)m_slot[0] + m_size[0];
        m_size[1] = p.m_rkey_size;

        m_slot[2] = (char*)m_slot[1] + m_size[1];
        m_size[2] = sizeof(std::uintptr_t);

        m_slot[3] = (char*)ptr + p.sender_status_flag_offset();
        m_size[3] = sizeof(unsigned int);
        m_flag_ptr = (unsigned int volatile*)m_slot[3];
    }

    unsigned int volatile& flag() noexcept { return *m_flag_ptr; }

    unsigned int flag() const noexcept { return *m_flag_ptr; }

    sender_status& set_worker_address(address_t const& addr) noexcept
    {
        std::memcpy((void*)m_slot[0], addr.data(), m_size[0]);
        return *this;
    }

    address_t get_worker_address() const noexcept
    {
        auto const first = (unsigned char*)m_slot[0];
        auto const last = first + m_size[0];
        return {first, last};
    }

    sender_status& set_status_rkey(void* rkey) noexcept
    {
        std::memcpy((void*)m_slot[1], rkey, m_size[1]);
        return *this;
    }

    void* get_status_rkey() const noexcept { return (char*)m_slot[1]; }

    sender_status& set_status_addr(void* ptr) noexcept
    {
        auto const addr = (std::uintptr_t)ptr;
        std::memcpy((void*)m_slot[2], &addr, m_size[2]);
        return *this;
    }

    std::uintptr_t get_status_addr() const noexcept
    {
        std::uintptr_t addr = 0;
        std::memcpy(&addr, (void const*)m_slot[2], m_size[2]);
        return addr;
    }
};

struct sender_status_msg
{
    // +----------------+--------------------+--------------------+-------------+-------------+
    // | worker address | status buffer rkey | status buffer addr | buffer rkey | buffer addr |
    // +----------------+--------------------+--------------------+-------------+-------------+

    std::array<void*, 5>       m_slot;
    std::array<std::size_t, 5> m_size;

    sender_status_msg(rma_params const& p, void* ptr) noexcept
    {
        m_slot[0] = ptr;
        m_size[0] = p.m_worker_address_size;

        m_slot[1] = (char*)m_slot[0] + m_size[0];
        m_size[1] = p.m_rkey_size;

        m_slot[2] = (char*)m_slot[1] + m_size[1];
        m_size[2] = sizeof(std::uintptr_t);

        m_slot[3] = (char*)m_slot[2] + m_size[2];
        m_size[3] = p.m_rkey_size;

        m_slot[4] = (char*)m_slot[3] + m_size[3];
        m_size[4] = sizeof(std::uintptr_t);
    }

    sender_status_msg& set_worker_address(address_t const& addr) noexcept
    {
        std::memcpy(m_slot[0], addr.data(), m_size[0]);
        return *this;
    }

    address_t get_worker_address() const noexcept
    {
        auto const first = (unsigned char*)m_slot[0];
        auto const last = first + m_size[0];
        return {first, last};
    }

    sender_status_msg& set_status_rkey(void* rkey) noexcept
    {
        std::memcpy(m_slot[1], rkey, m_size[1]);
        return *this;
    }

    void* get_status_rkey() const noexcept { return (char*)m_slot[1]; }

    sender_status_msg& set_status_addr(void* ptr) noexcept
    {
        auto const addr = (std::uintptr_t)ptr;
        std::memcpy(m_slot[2], &addr, m_size[2]);
        return *this;
    }

    std::uintptr_t get_status_addr() const noexcept
    {
        std::uintptr_t addr = 0;
        std::memcpy(&addr, m_slot[2], m_size[2]);
        return addr;
    }

    sender_status_msg& set_buffer_rkey(void* rkey) noexcept
    {
        std::memcpy(m_slot[3], rkey, m_size[3]);
        return *this;
    }

    void* get_buffer_rkey() const noexcept { return (char*)m_slot[3]; }

    sender_status_msg& set_buffer_addr(void* ptr) noexcept
    {
        auto const addr = (std::uintptr_t)ptr;
        std::memcpy(m_slot[4], &addr, m_size[4]);
        return *this;
    }

    std::uintptr_t get_buffer_addr() const noexcept
    {
        std::uintptr_t addr = 0;
        std::memcpy(&addr, m_slot[4], m_size[4]);
        return addr;
    }
};

struct rma_request
{
    void*        m_req = nullptr;
    ucp_worker_h m_worker;
    bool         m_is_ready = false;

    rma_request() = default;

    rma_request(void* req, ucp_worker_h worker)
    : m_req{req == NULL ? nullptr : req}
    , m_worker{worker}
    , m_is_ready{req == NULL ? true : false}
    {
        if (m_req)
        {
            if (UCS_PTR_IS_ERR(m_req)) throw std::runtime_error("oomph rma ucx error");
        }
    }

    rma_request(rma_request&& other) noexcept
    : m_req{std::exchange(other.m_req, nullptr)}
    , m_worker{other.m_worker}
    , m_is_ready{std::exchange(other.m_is_ready, false)}
    {
    }

    rma_request& operator=(rma_request&& other) noexcept
    {
        if (m_req) ucp_request_release(m_req);
        m_req = std::exchange(other.m_req, nullptr);
        m_worker = other.m_worker;
        m_is_ready = std::exchange(other.m_is_ready, false);
        return *this;
    }

    bool is_ready() const noexcept { return m_is_ready; }

    bool test()
    {
        if (is_ready()) return true;
        if (!m_req) return false;
        ucp_worker_progress(m_worker);
        auto status = ucp_request_check_status(m_req);
        if (status != UCS_INPROGRESS)
        {
            ucp_request_release(m_req);
            m_req = nullptr;
            m_is_ready = true;
            return true;
        }
        else
            return false;
    }
};

static void empty_send_callback(void*, ucs_status_t) {}

} // namespace channel
} // namespace oomph
