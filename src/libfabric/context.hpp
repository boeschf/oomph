/*
 * ghex-org
 *
 * Copyright (c) 2014-2022, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <thread>

#include <hwmalloc/heap.hpp>
#include <hwmalloc/register.hpp>
//
#include "memory_region.hpp"
#include "../context_base.hpp"

namespace oomph
{

using controller_type = oomph::libfabric::controller;

class context_impl : public context_base
{
  public:
    using region_type = oomph::libfabric::memory_segment;
    using domain_type = region_type::provider_domain;
    using device_region_type = oomph::libfabric::memory_segment;
    using heap_type = hwmalloc::heap<context_impl>;
    using rank_type = communicator::rank_type;
    using tag_type = communicator::tag_type;

  private:
    heap_type                        m_heap;
    domain_type*                     m_domain;
    std::shared_ptr<controller_type> m_controller;

  public:
    // --------------------------------------------------
    // create a singleton ptr to a libfabric controller that
    // can be shared between oomph context objects
    static std::shared_ptr<controller_type> init_libfabric_controller(oomph::context_impl* ctx,
        MPI_Comm comm, int rank, int size, int threads);

  public:
    context_impl(MPI_Comm comm, bool thread_safe);
    context_impl(context_impl const&) = delete;
    context_impl(context_impl&&) = delete;

    region_type make_region(void* const ptr,
        std::size_t                     size /*, hwmalloc::registration_flags flags*/)
    {
        return oomph::libfabric::memory_segment(m_domain, ptr, size /*, flags*/);
    }

    auto& get_heap() noexcept { return m_heap; }

    communicator_impl* get_communicator();

    inline controller_type* get_controller() /*const */ { return m_controller.get(); }
    const char*             get_transport_option(const std::string& opt);
};

// --------------------------------------------------------------------
template<>
oomph::libfabric::memory_segment
register_memory<oomph::context_impl>(oomph::context_impl& c, void* const ptr, std::size_t size)
{
    return c.make_region(ptr, size /*, flags*/);
}

#if HWMALLOC_ENABLE_DEVICE
template<>
region
register_device_memory<context_impl>(context_impl& c, void* ptr, std::size_t)
{
    return c.make_region(ptr);
}
#endif

} // namespace oomph
