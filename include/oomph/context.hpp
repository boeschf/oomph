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

#include <oomph/util/mpi_comm_holder.hpp>
#include <oomph/util/heap_pimpl.hpp>
#include <oomph/message_buffer.hpp>
#include <oomph/communicator.hpp>
#include <oomph/future.hpp>
#include <oomph/channel/sender.hpp>
#include <oomph/channel/receiver.hpp>
#include <hwmalloc/config.hpp>
#include <hwmalloc/device.hpp>

namespace oomph
{
class context_impl;
class barrier;
class context
{
    friend class barrier;

  public:
    using pimpl = util::heap_pimpl<context_impl>;
    using rank_type = communicator::rank_type;
    using tag_type = communicator::tag_type;

  private:
    util::mpi_comm_holder m_mpi_comm;
    pimpl                 m;

  public:
    context(MPI_Comm comm, bool thread_safe = true);

    context(context const&) = delete;

    context(context&&) noexcept;

    context& operator=(context const&) = delete;

    context& operator=(context&&) noexcept;

    ~context();

  public:
    MPI_Comm mpi_comm() const noexcept { return m_mpi_comm.get(); }

    template<typename T>
    message_buffer<T> make_buffer(std::size_t size)
    {
        return {make_buffer_core(size * sizeof(T)), size};
    }

    template<typename T>
    message_buffer<T> make_buffer(T* ptr, std::size_t size)
    {
        return {make_buffer_core(ptr, size * sizeof(T)), size};
    }

#if HWMALLOC_ENABLE_DEVICE
    template<typename T>
    message_buffer<T> make_device_buffer(std::size_t size, int id = hwmalloc::get_device_id())
    {
        return {make_buffer_core(size * sizeof(T), id), size};
    }

    template<typename T>
    message_buffer<T> make_device_buffer(T* device_ptr, std::size_t size,
        int id = hwmalloc::get_device_id())
    {
        return {make_buffer_core(device_ptr, size * sizeof(T), id), size};
    }

    template<typename T>
    message_buffer<T> make_device_buffer(T* ptr, T* device_ptr, std::size_t size,
        int id = hwmalloc::get_device_id())
    {
        return {make_buffer_core(ptr, device_ptr, size * sizeof(T), id), size};
    }
#endif

    communicator get_communicator();

    template<typename T>
    future<channel::sender<T>> make_sender(std::size_t size, rank_type r, tag_type t)
    {
        return channel::sender<T>{get_communicator(), size, r, t};
    }

    template<typename T>
    future<channel::receiver<T>> make_receiver(std::size_t size, rank_type r, tag_type t)
    {
        return channel::receiver<T>{get_communicator(), size, r, t};
    }

  private:
    detail::message_buffer make_buffer_core(std::size_t size);
    detail::message_buffer make_buffer_core(void* ptr, std::size_t size);
#if HWMALLOC_ENABLE_DEVICE
    detail::message_buffer make_buffer_core(std::size_t size, int device_id);
    detail::message_buffer make_buffer_core(void* device_ptr, std::size_t size, int device_id);
    detail::message_buffer make_buffer_core(void* ptr, void* device_ptr, std::size_t size,
        int device_id);
#endif
};

template<typename Context>
typename Context::region_type register_memory(Context&, void*, std::size_t);
#if HWMALLOC_ENABLE_DEVICE
template<typename Context>
typename Context::device_region_type register_device_memory(Context&, void*, std::size_t);
#endif

} // namespace oomph
