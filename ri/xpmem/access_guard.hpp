/*
 * GridTools
 *
 * Copyright (c) 2014-2020, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef INCLUDED_GHEX_TRANSPORT_LAYER_RI_XPMEM_ACCESS_GUARD_HPP
#define INCLUDED_GHEX_TRANSPORT_LAYER_RI_XPMEM_ACCESS_GUARD_HPP

#include <mutex>
#include <condition_variable>
#include <memory>

extern "C"{
#include <mpi.h>
#include <xpmem.h>
#include <unistd.h>
}

namespace gridtools {
namespace ghex {
namespace tl {
namespace ri {
namespace xpmem {

// a finite-state machine that guards alternating either the local or remote site
// must be initialized with the init function
// initialization completion can be checked with ready function
// for now this is only for threads, but should be made more general for processes (and
// remote processes)
struct access_guard
{
    enum access_mode
    {
        local,
        remote
    };

    unsigned char volatile *ptr = NULL;
    xpmem_segid_t           xpmem_endpoint = -1;

    access_guard() {
    }

    void init() {
	size_t pagesize = getpagesize();
	if(0 != posix_memalign((void**)&ptr, pagesize, pagesize)){
	    fprintf(stderr, "cannot allocate access_guard\n");
	    exit(1);
	}

	ptr[0] = local;
	
	/* publish pointer */
	xpmem_endpoint = xpmem_make((void*)ptr, pagesize, XPMEM_PERMIT_MODE, (void*)0666);
	if(xpmem_endpoint<0){
	    fprintf(stderr, "error registering xpmem endpoint\n");
	}
    }

    bool ready() {
        return true;
    }
};

// a view on an access guard
// does not own any resources
// exposes necessary functions to lock and unlock remote/local sites
struct access_guard_view
{
    access_guard m_impl;

    access_guard_view() = default;

    access_guard_view(access_guard& g) : m_impl{g} {
    }

    void init() {

	/* init deserialized guard */
	int pagesize = getpagesize();
	struct xpmem_addr addr;
	addr.offset = 0;
	addr.apid   = xpmem_get(m_impl.xpmem_endpoint, XPMEM_RDWR, XPMEM_PERMIT_MODE, NULL);
	m_impl.ptr = (unsigned char*)xpmem_attach(addr, pagesize, NULL);
    }	
    
    bool ready() {
	return true;
    }

    void start_remote_epoch() {
	while(access_guard::remote != m_impl.ptr[0]){
	    // TODO call comm.progress()
	    sched_yield();
	};
    }

    void end_remote_epoch() {
	m_impl.ptr[0] = access_guard::local;
    }

    void start_local_epoch() {
	while(access_guard::local != m_impl.ptr[0]){
	    // TODO call comm.progress()
	    sched_yield();
	};
    }

    void end_local_epoch() {
	m_impl.ptr[0] = access_guard::remote;
    }
};

} // namespace xpmem
} // namespace ri
} // namespace tl
} // namespace ghex
} // namespace gridtools

#endif /* INCLUDED_GHEX_TRANSPORT_LAYER_RI_XPMEM_ACCESS_GUARD_HPP */
