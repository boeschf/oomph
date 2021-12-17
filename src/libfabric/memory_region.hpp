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

#include <hwmalloc/register.hpp>
//
#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_errno.h>
//
#include <iostream>
#include <memory>
#include <utility>
//
#if __has_include("./print.hpp")
#   include "./print.hpp"
    // cppcheck-suppress ConfigurationNotChecked
    static hpx::debug::enable_print<false> mrn_deb("REGION_");
#  define has_debug 1
#else
#endif

namespace oomph { namespace libfabric
{

// This is the ony part of the code that actually
// calls libfabric functions
struct region_provider
{
    // The internal memory region handle
    typedef struct fid_mr     provider_region;
    typedef struct fid_domain provider_domain;

    /*
     * Correct
     * fi_mr_reg(struct fid_domain *domain, const void *buf, size_t len,
     *      uint64_t acs, uint64_t offset, uint64_t requested_key,
     *      uint64_t flags, struct fid_mr **mr, void *context)
    */

    // register region
    template<typename... Args>
    static int register_memory(Args&&... args)
    {
        return fi_mr_reg(std::forward<Args>(args)...);
    }

    // unregister region
    static int unregister_memory(provider_region* region) { return fi_close(&region->fid); }

    // Default registration flags for this provider
    static int flags()
    {
        return FI_READ | FI_WRITE | FI_RECV | FI_SEND | FI_REMOTE_READ | FI_REMOTE_WRITE;
    }

    // Get the local descriptor of the memory region.
    static void* get_local_key(provider_region * const region) { return fi_mr_desc(region); }

    // Get the remote key of the memory region.
    static uint64_t get_remote_key(provider_region * const region) { return fi_mr_key(region); }
};

struct handle_base
{
    virtual ~handle_base() {}
    virtual uint64_t       get_remote_key(void) const = 0;
    virtual void          *get_local_key(void) const = 0;
    virtual unsigned char *get_address(void) const = 0;
    virtual uint32_t get_message_length(void) const { return 0; }
    virtual void     set_message_length(uint32_t) { }
};

// --------------------------------------------------------------------
// This is a handle to a small chunk of memory that has been registered
// as part of a much larger allocation (a memory_segment)
struct memory_handle : handle_base
{
    // --------------------------------------------------------------------
    using provider_region = region_provider::provider_region;

    // --------------------------------------------------------------------
    // Default constructor creates unusable handle(region)
    memory_handle()
        : address_{nullptr}
        , region_{nullptr}
        , size_{0}
        , used_space_{0}
    {}
    memory_handle(memory_handle const&) noexcept = default;
    memory_handle& operator=(memory_handle const&) noexcept = default;

    memory_handle(provider_region* region, unsigned char* addr, std::size_t size/*, uint32_t flags*/) noexcept
        : address_{addr}
        , region_{region}
        , size_{uint32_t(size)}
        , used_space_{0}
//        , flags_{flags}
    {
    }

    // --------------------------------------------------------------------
    // move constructor, clear other region so that it is not unregistered twice
    memory_handle(memory_handle&& other) noexcept
        : address_{other.address_}
        , region_{std::exchange(other.region_, nullptr)}
        , size_{other.size_}
        , used_space_{other.used_space_}
//        , flags_{other.flags_}
    {
    }

    // --------------------------------------------------------------------
    // move assignment, clear other region so that it is not unregistered twice
    memory_handle& operator=(memory_handle&& other) noexcept
    {
        address_ = other.address_;
        region_ = std::exchange(other.region_, nullptr);
        size_ = other.size_;
        used_space_ = other.used_space_;
//        flags_ = other.flags_;
        return *this;
    }

    // --------------------------------------------------------------------
    // Return the address of this memory region block.
    inline unsigned char* get_address(void) const { return address_; }

    // --------------------------------------------------------------------
    // Get the local descriptor of the memory region.
    inline void* get_local_key(void) const { return region_provider::get_local_key(region_); }

    // --------------------------------------------------------------------
    // Get the remote key of the memory region.
    inline uint64_t get_remote_key(void) const { return region_provider::get_remote_key(region_); }

    // --------------------------------------------------------------------
    // Get the size of the memory chunk usable by this memory region,
    // this may be smaller than the value returned by get_length
    // if the region is a sub region (partial region) within another block
    inline uint64_t get_size(void) const { return size_; }

    // --------------------------------------------------------------------
    // Get the size used by a message in the memory region.
    inline uint32_t get_message_length(void) const { return size_ /*used_space_*/; }

    // --------------------------------------------------------------------
    // Set the size used by a message in the memory region.
    inline void set_message_length(uint32_t length) { used_space_ = length; }

    // --------------------------------------------------------------------
    // A user allocated region uses memory allocted by the user.
    // on destruction, the memory is unregistered, but not deleted
//    inline void set_user_region() { flags_ |= hwmalloc::registration_flags::memory_user; }

//    inline bool get_user_region() const { return (flags_ & hwmalloc::registration_flags::memory_user) == hwmalloc::registration_flags::memory_user; }
/*
    // --------------------------------------------------------------------
    // A temp region is one that the memory pool is not managing
    // so it is unregistered and deleted when returned to the pool and not reused
    inline void set_temp_region() { flags_ |= BLOCK_TEMP; }

    inline bool get_temp_region() const { return (flags_ & BLOCK_TEMP) == BLOCK_TEMP; }

    // --------------------------------------------------------------------
    // a partial region is a subregion of a larger memory region
    // on destruction, it is not unregistered or deleted as the 'parent' region
    // will delete many partial regions on destruction
    inline void set_partial_region() { flags_ |= BLOCK_PARTIAL; }

    inline bool get_partial_region() const { return (flags_ & BLOCK_PARTIAL) == BLOCK_PARTIAL; }
*/

    // --------------------------------------------------------------------
    void release_region() noexcept
    {
        region_ = nullptr;
    }

    // --------------------------------------------------------------------
    // return the underlying libfabric region handle
    inline provider_region* get_region() const { return region_; }

    // --------------------------------------------------------------------
    friend std::ostream& operator<<(std::ostream& os, memory_handle const& region)
    {
#if has_debug
        // clang-format off
        os << "region "      << hpx::debug::ptr(&region)
           << " fi_region "  << hpx::debug::ptr(region.region_)
           << " address "    << hpx::debug::ptr(region.address_)
           << " size "       << hpx::debug::hex<6>(region.size_)
           << " used_space " << hpx::debug::hex<6>(region./*used_space_*/size_)
           << " local key "  << hpx::debug::ptr(region_provider::get_local_key(region.region_))
           << " remote key " << hpx::debug::ptr(region_provider::get_remote_key(region.region_))
//           << " flags "      << hpx::debug::bin<8>(region.flags_);
              ;
        // clang-format on
#endif
        return os;
    }

  protected:
    // This gives the start address of this region.
    // This is the address that should be used for data storage
    unsigned char * address_;

    // The hardware level handle to the region (as returned from libfabric fi_mr_reg)
    provider_region* region_;

    // The (maximum available) size of the memory buffer
    uint32_t size_;

    // Space used by a message in the memory region.
    // This may be smaller/less than the size available if more space
    // was allocated than it turns out was needed
    mutable uint32_t used_space_;

    // flags to control lifetime of blocks
//    uint32_t flags_;

};

// --------------------------------------------------------------------
// a memory segment is a pinned block of memory that has been specialized
// by a particular region provider. Each provider (infiniband, libfabric,
// other) has a different definition for the object and the protection
// domain used to limit access.
// --------------------------------------------------------------------
struct memory_segment : public memory_handle
{
    using provider_domain = region_provider::provider_domain;
    using provider_region = region_provider::provider_region;
    using handle_type = memory_handle;

    // --------------------------------------------------------------------
    memory_segment(
        provider_region* region, unsigned char* address, unsigned char* base_address, uint64_t size/*, hwmalloc::registration_flags flags*/)
    : memory_handle(region, address, size/*, flags*/)
    , base_addr_(base_address)
    {
    }

    // --------------------------------------------------------------------
    // move constructor, clear other region
    memory_segment(memory_segment&& other) noexcept
    : memory_handle(std::move(other))
    , base_addr_{std::exchange(other.base_addr_, nullptr)}
    {
    }

    // --------------------------------------------------------------------
    // move assignment, clear other region
    memory_segment& operator=(memory_segment&& other) noexcept
    {
        memory_handle(std::move(other));
        region_ = std::exchange(other.region_, nullptr);
        return *this;
    }

    // --------------------------------------------------------------------
    // construct a memory region object by registering an existing address buffer
    // we do not cache local/remote keys here because memory segments are only
    // used by the heap to store chunks and the user will always receive
    // a memory_handle - which does have keys cached
    memory_segment(provider_domain* pd, const void* buffer, const uint64_t length/*, hwmalloc::registration_flags flags*/)
    {
        address_ = static_cast<unsigned char*>(const_cast<void*>(buffer));
        size_ = length;
        used_space_ = length;
        region_ = nullptr;
//        flags_ = flags;
        //
        base_addr_ = memory_handle::address_;

        int ret = region_provider::register_memory(pd, const_cast<void*>(buffer), length,
            region_provider::flags(), 0, (uint64_t)address_, 0, &(region_), nullptr);

#if has_debug
        if (ret)
        {
            mrn_deb.error(hpx::debug::str<>("Registering region"),
                hpx::debug::ptr(buffer), hpx::debug::hex<6>(length));
        }
        else
        {
            // clang-format off
            mrn_deb.trace(hpx::debug::str<>("Registered region"), hpx::debug::ptr(buffer),
                          "desc ", hpx::debug::ptr(fi_mr_desc(region_)),
                          "rkey ", hpx::debug::ptr(fi_mr_key(region_)),
                          "length ", hpx::debug::hex<6>(size_));
            // clang-format on
        }
#endif
    }

    // --------------------------------------------------------------------
    // destroy the region and memory according to flag settings
    ~memory_segment()
    {
        //            if (get_partial_region())
        //                return;
        deregister();
    }

    // --------------------------------------------------------------------
    // Deregister the memory region.
    // returns 0 when successful, -1 otherwise
    int deregister(void)
    {
        if (region_ /*&& !get_user_region()*/)
        {
#if has_debug
            mrn_deb.trace(hpx::debug::str<>("release"), region_);
            // get these before deleting/unregistering (for logging)
            auto length = mrn_deb.declare_variable<uint64_t>(get_size());
            (void) length;
#endif
            //
            if (region_provider::unregister_memory(region_))
            {
#if has_debug
                mrn_deb.error("fi_close mr failed");
#endif
                return -1;
            }
            else
            {
#if has_debug
                mrn_deb.trace(hpx::debug::str<>("deregistered"), region_);
#endif
            }
//            if (!get_user_region())
//            {
//                //                    delete[](static_cast<const char*>(buffer));
//            }
            region_ = nullptr;
        }
        return 0;
    }

    handle_type get_handle(std::size_t offset, std::size_t size) const noexcept
    {
        return memory_handle(region_, base_addr_ + offset, size/*, flags_*/);
    }

    // --------------------------------------------------------------------
    // Get the address of the base memory region.
    // This is the address of the memory allocated from the system
    inline unsigned char* get_base_address(void) const { return base_addr_; }

    // --------------------------------------------------------------------
    friend std::ostream& operator<<(std::ostream& os, memory_segment const& region)
    {
#if has_debug
        // clang-format off
        os << *static_cast<const memory_handle*>(&region)
           << " base address " << hpx::debug::ptr(region.base_addr_);
        // clang-format on
#endif
        return os;
    }

  public:
    // this is the base address of the memory registered by this segment
    // individual memory_handles are offset from this address
    unsigned char* base_addr_;
};

} // namespace libfabric
} // namespace oomph
