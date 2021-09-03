/*
 * GridTools
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
#include <hwmalloc/config.hpp>
#include <hwmalloc/device.hpp>
#include <hwmalloc/register.hpp>

namespace oomph
{
class context_impl;
class context
{
  public:
    using pimpl = util::heap_pimpl<context_impl>;

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

#if HWMALLOC_ENABLE_DEVICE
    template<typename T>
    message_buffer<T> make_device_buffer(std::size_t size, int id = hwmalloc::get_device_id())
    {
        return {make_buffer_core(size * sizeof(T), id), size};
    }
#endif

    communicator get_communicator();

  private:
    detail::message_buffer make_buffer_core(std::size_t size);
#if HWMALLOC_ENABLE_DEVICE
    detail::message_buffer make_buffer_core(std::size_t size, int device_id);
#endif
};

template<typename Context>
typename Context::region_type register_memory(Context&, void*, std::size_t, hwmalloc::registration_flags);
#if HWMALLOC_ENABLE_DEVICE
template<typename Context>
typename Context::device_region_type register_device_memory(Context&, void*, std::size_t);
#endif

} // namespace oomph
