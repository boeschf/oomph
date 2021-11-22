/*
 * ghex-org
 *
 * Copyright (c) 2014-2021, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <hwmalloc/numa.hpp>
#if HWMALLOC_ENABLE_DEVICE
#include <hwmalloc/device.hpp>
#endif
#include <oomph/config.hpp>
#include <oomph/util/heap_pimpl.hpp>
#include <oomph/util/stack_pimpl.hpp>
#if OOMPH_ENABLE_BARRIER
#include <oomph/barrier.hpp>
#include "./thread_id.hpp"
#include <map>
#include <set>
#include <mutex>
#endif // OOMPH_ENABLE_BARRIER

namespace oomph
{
#if OOMPH_ENABLE_BARRIER
///////////////////////////////
// thread_id                 //
///////////////////////////////
namespace
{
std::uintptr_t*
alloc_tid_m()
{
    auto ptr = new std::uintptr_t{};
    *ptr = (std::uintptr_t)ptr;
    return ptr;
}
} // namespace

thread_id::thread_id()
: m{alloc_tid_m()}
{
}

thread_id::~thread_id() { delete m; }

thread_id const&
tid()
{
    static thread_local thread_id id;
    return id;
}

namespace
{
std::mutex                                                                            comm_map_mtx;
std::map<context_impl const*, std::map<std::uintptr_t, std::set<communicator_impl*>>> comm_map;
} // namespace

auto&
get_comm_set(context_impl const* ctxt)
{
    auto const&                 _tid = tid();
    std::lock_guard<std::mutex> lock(comm_map_mtx);
    return comm_map[ctxt][_tid];
}
#endif // OOMPH_ENABLE_BARRIER

///////////////////////////////
// context                   //
///////////////////////////////

context::context(MPI_Comm comm, bool thread_safe)
: m_mpi_comm{comm}
, m(m_mpi_comm.get(), thread_safe)
{
}

context::context(context&&) noexcept = default;

context& context::operator=(context&&) noexcept = default;

#if OOMPH_ENABLE_BARRIER
context::~context() { comm_map.erase(m.get()); }
#else
context::~context() = default;
#endif

communicator
context::get_communicator()
{
    return {m->get_communicator()};
}

///////////////////////////////
// communicator              //
///////////////////////////////

communicator::communicator(impl_type* impl_)
: m_impl{impl_}
, m_pool{std::make_unique<boost::pool<>>(sizeof(detail::request_state), 128)}
, m_schedule{std::make_unique<schedule>()}
{
#if OOMPH_ENABLE_BARRIER
    get_comm_set(m_impl->m_context).insert(m_impl);
#endif // OOMPH_ENABLE_BARRIER
}

communicator::~communicator()
{
    if (m_impl)
    {
        wait_all();
        m_impl->release();
#if OOMPH_ENABLE_BARRIER
        get_comm_set(m_impl->m_context).erase(m_impl);
#endif // OOMPH_ENABLE_BARRIER
    }
}

communicator::rank_type
communicator::rank() const noexcept
{
    return m_impl->rank();
}

communicator::rank_type
communicator::size() const noexcept
{
    return m_impl->size();
}

bool
communicator::is_local(rank_type rank) const noexcept
{
    return m_impl->is_local(rank);
}

MPI_Comm
communicator::mpi_comm() const noexcept
{
    return m_impl->mpi_comm();
}

void
communicator::progress()
{
    m_impl->progress();
}

#if OOMPH_ENABLE_BARRIER
///////////////////////////////
// barrier                   //
///////////////////////////////
barrier::barrier(context const& c, size_t n_threads)
: m_threads{n_threads}
, b_count2{m_threads}
, m_mpi_comm{c.mpi_comm()}
, m_context{c.m.get()}
{
}

void
barrier::operator()() const
{
    if (in_node1()) rank_barrier();
    else
        while (b_count2 == m_threads)
            for (auto c : get_comm_set(m_context)) c->progress();
    in_node2();
}

void
barrier::rank_barrier() const
{
    MPI_Request req = MPI_REQUEST_NULL;
    int         flag;
    MPI_Ibarrier(m_mpi_comm, &req);
    while (true)
    {
        for (auto c : get_comm_set(m_context)) c->progress();
        MPI_Test(&req, &flag, MPI_STATUS_IGNORE);
        if (flag) break;
    }
}

bool
barrier::in_node1() const
{
    size_t expected = b_count;
    while (!b_count.compare_exchange_weak(expected, expected + 1, std::memory_order_relaxed))
        expected = b_count;
    if (expected == m_threads - 1)
    {
        b_count.store(0);
        return true;
    }
    else
    {
        while (b_count != 0)
            for (auto c : get_comm_set(m_context)) c->progress();
        return false;
    }
}

void
barrier::in_node2() const
{
    size_t ex = b_count2;
    while (!b_count2.compare_exchange_weak(ex, ex - 1, std::memory_order_relaxed)) ex = b_count2;
    if (ex == 1) { b_count2.store(m_threads); }
    else
    {
        while (b_count2 != m_threads)
            for (auto c : get_comm_set(m_context)) c->progress();
    }
}
#endif // OOMPH_ENABLE_BARRIER

///////////////////////////////
// message_buffer            //
///////////////////////////////

namespace detail
{
using heap_ptr = typename context_impl::heap_type::pointer;

class message_buffer::heap_ptr_impl
{
  public:
    heap_ptr m;
    void     release() { m.release(); }
};

message_buffer::message_buffer() = default;

template<>
message_buffer::message_buffer(typename context_impl::heap_type::pointer ptr)
: m_ptr{ptr.get()}
, m_heap_ptr(ptr)
{
}

message_buffer::message_buffer(message_buffer&& other)
: m_ptr{std::exchange(other.m_ptr, nullptr)}
, m_heap_ptr{std::move(other.m_heap_ptr)}
{
}

message_buffer::~message_buffer()
{
    if (m_ptr) m_heap_ptr->release();
}

message_buffer&
message_buffer::operator=(message_buffer&& other)
{
    if (m_ptr) m_heap_ptr->release();
    m_ptr = std::exchange(other.m_ptr, nullptr);
    m_heap_ptr = std::move(other.m_heap_ptr);
    return *this;
}

bool
message_buffer::on_device() const noexcept
{
    return m_heap_ptr->m.on_device();
}

#if HWMALLOC_ENABLE_DEVICE
void*
message_buffer::device_data() noexcept
{
    return m_heap_ptr->m.device_ptr();
}

void const*
message_buffer::device_data() const noexcept
{
    return m_heap_ptr->m.device_ptr();
}

int
message_buffer::device_id() const noexcept
{
    return m_heap_ptr->m.device_id();
}

void
message_buffer::clone_to_device(std::size_t count)
{
    hwmalloc::memcpy_to_device(m_heap_ptr->m.device_ptr(), m_ptr, count);
}

void
message_buffer::clone_to_host(std::size_t count)
{
    hwmalloc::memcpy_to_host(m_ptr, m_heap_ptr->m.device_ptr(), count);
}
#endif

void
message_buffer::clear()
{
    m_ptr = nullptr;
    m_heap_ptr = context_impl::heap_type::pointer{nullptr};
}

} // namespace detail

///////////////////////////////
// communicator              //
///////////////////////////////

void
communicator::send(detail::message_buffer::heap_ptr_impl const* m_ptr, std::size_t size,
    rank_type dst, tag_type tag, util::unique_function<void()> cb, shared_request_ptr req)
{
    m_impl->send(m_ptr->m, size, dst, tag, std::move(cb), std::move(req));
}

void
communicator::recv(detail::message_buffer::heap_ptr_impl* m_ptr, std::size_t size, rank_type src,
    tag_type tag, util::unique_function<void()> cb, shared_request_ptr req)
{
    m_impl->recv(m_ptr->m, size, src, tag, std::move(cb), std::move(req));
}

///////////////////////////////
// make_buffer               //
///////////////////////////////

detail::message_buffer
context::make_buffer_core(std::size_t size)
{
    return m->get_heap().allocate(size, hwmalloc::numa().local_node());
}

detail::message_buffer
context::make_buffer_core(void* ptr, std::size_t size)
{
    return m->get_heap().register_user_allocation(ptr, size);
}

#if HWMALLOC_ENABLE_DEVICE
detail::message_buffer
context::make_buffer_core(std::size_t size, int id)
{
    return m->get_heap().allocate(size, hwmalloc::numa().local_node(), id);
}

detail::message_buffer
context::make_buffer_core(void* device_ptr, std::size_t size, int device_id)
{
    return m->get_heap().register_user_allocation(device_ptr, device_id, size);
}

detail::message_buffer
context::make_buffer_core(void* ptr, void* device_ptr, std::size_t size, int device_id)
{
    return m->get_heap().register_user_allocation(ptr, device_ptr, device_id, size);
}
#endif

detail::message_buffer
communicator::make_buffer_core(std::size_t size)
{
    return m_impl->get_heap().allocate(size, hwmalloc::numa().local_node());
}

detail::message_buffer
communicator::make_buffer_core(void* ptr, std::size_t size)
{
    return m_impl->get_heap().register_user_allocation(ptr, size);
}

#if HWMALLOC_ENABLE_DEVICE
detail::message_buffer
communicator::make_buffer_core(std::size_t size, int id)
{
    return m_impl->get_heap().allocate(size, hwmalloc::numa().local_node(), id);
}

detail::message_buffer
communicator::make_buffer_core(void* device_ptr, std::size_t size, int device_id)
{
    return m_impl->get_heap().register_user_allocation(device_ptr, device_id, size);
}

detail::message_buffer
communicator::make_buffer_core(void* ptr, void* device_ptr, std::size_t size, int device_id)
{
    return m_impl->get_heap().register_user_allocation(ptr, device_ptr, device_id, size);
}
#endif

/////////////////////////////////
//// request                   //
/////////////////////////////////

bool
send_request::test()
{
    if (!m_data) return true;
    if (m_data->m_ready) return true;
    m_data->m_comm->progress();
    return is_ready();
}

void
send_request::wait()
{
    if (!m_data) return;
    while (!m_data->m_ready) m_data->m_comm->progress();
}

bool
recv_request::test()
{
    if (!m_data) return true;
    if (m_data->m_ready) return true;
    m_data->m_comm->progress();
    return is_ready();
}

void
recv_request::wait()
{
    if (!m_data) return;
    while (!m_data->m_ready) m_data->m_comm->progress();
}

bool
recv_request::cancel()
{
    if (!m_data) return false;
    if (m_data->m_ready) return false;
    const auto res = m_data->m_comm->cancel_recv_cb(*this);
    if (res)
    {
        --(*(m_data->m_scheduled));
        m_data.reset();
    }
    return res;
}

/////////////////////////////////
//// send_channel_base         //
/////////////////////////////////
//
//send_channel_base::~send_channel_base() = default;
//
//void
//send_channel_base::connect()
//{
//    m_impl->connect();
//}
//
/////////////////////////////////
//// recv_channel_base         //
/////////////////////////////////
//
//recv_channel_base::~recv_channel_base() = default;
//
//void
//recv_channel_base::connect()
//{
//    m_impl->connect();
//}
//
//std::size_t
//recv_channel_base::capacity()
//{
//    return m_impl->capacity();
//}
//
//void*
//recv_channel_base::get(std::size_t& index)
//{
//    return m_impl->get(index);
//}
//
//recv_channel_impl*
//recv_channel_base::get_impl() noexcept
//{
//    return &(*m_impl);
//}

} // namespace oomph
