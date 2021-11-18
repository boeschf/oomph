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

#include "./handle.hpp"

namespace oomph
{
class region
{
  public:
    //using handle_type = handle;
    struct handle_type
    {
    };

  private:
    void* m_ptr;

  public:
    region(void* ptr)
    : m_ptr{ptr}
    {
    }

    region(region const&) = delete;

    region(region&& r) noexcept
    : m_ptr{std::exchange(r.m_ptr, nullptr)}
    {
    }

    // get a handle to some portion of the region
    handle_type get_handle(std::size_t/* offset*/, std::size_t/* size*/)
    {
        //return {(void*)((char*)m_ptr + offset), size};
        return {};
    }
};

class rma_region
{
  public:
    using handle_type = handle;

  private:
    ucp_context_h m_ucp_context;
    void*         m_ptr;
    std::size_t   m_size;
    ucp_mem_h     m_memh;
    void*         m_rkey;
    size_t        m_rkey_size;

  public:
    rma_region(ucp_context_h ctxt, void* ptr, std::size_t size, bool gpu = false)
    : m_ucp_context{ctxt}
    , m_ptr{ptr}
    , m_size{size}
    {
        ucp_mem_map_params_t params;

        // enable fields
        /* clang-format off */
        params.field_mask = 
              UCP_MEM_MAP_PARAM_FIELD_ADDRESS     // enable address field
            | UCP_MEM_MAP_PARAM_FIELD_LENGTH      // enable length field
        //  | UCP_MEM_MAP_PARAM_FIELD_FLAGS       // enable flags field
#if (UCP_API_VERSION >= 17432576)                 // version >= 1.10
            | UCP_MEM_MAP_PARAM_FIELD_MEMORY_TYPE // enable memory type field
#endif
        ;
        /* clang-format on */

        // set fields
        params.address = ptr;
        params.length = size;
#if (UCP_API_VERSION >= 17432576) // version >= 1.10
        params.memory_type = UCS_MEMORY_TYPE_HOST;
#endif

        // special treatment for gpu memory
#if HWMALLOC_ENABLE_DEVICE | !defined(HWMALLOC_DEVICE_EMULATE)
        if (gpu)
        {
#if (UCP_API_VERSION >= 17432576) // version >= 1.10
#if defined(HWMALLOC_DEVICE_CUDA)
            params.memory_type = UCS_MEMORY_TYPE_CUDA;
#elif defined(HWMALLOC_DEVICE_HIP)
            params.memory_type = UCS_MEMORY_TYPE_ROCM;
#endif
#endif
        }
#endif

        // register memory
        OOMPH_CHECK_UCX_RESULT(ucp_mem_map(m_ucp_context, &params, &m_memh));

        // pack memory handle
        OOMPH_CHECK_UCX_RESULT(ucp_rkey_pack(m_ucp_context,m_memh,&m_rkey,&m_rkey_size));
    }

    rma_region(rma_region const&) = delete;
    rma_region(rma_region&& r) noexcept
    : m_ucp_context{r.m_ucp_context}
    , m_ptr{std::exchange(r.m_ptr, nullptr)}
    , m_size{r.m_size}
    , m_memh{r.m_memh}
    , m_rkey{r.m_rkey}
    , m_rkey_size{r.m_rkey_size}
    {
    }
    ~rma_region()
    {
        if (m_ptr)
        {
            ucp_rkey_buffer_release(m_rkey);
            ucp_mem_unmap(m_ucp_context, m_memh);
        }
    }

    // get a handle to some portion of the region
    handle_type get_handle(std::size_t offset, std::size_t size)
    {
        //return {(void*)((char*)m_ptr + offset), size};
        return {m_rkey, m_rkey_size, offset};
    }
};

} // namespace oomph
