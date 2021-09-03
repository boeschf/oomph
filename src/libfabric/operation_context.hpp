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

#include <rdma/fabric.h>
//
#include <oomph/util/unique_function.hpp>
//
#include <boost/lockfree/queue.hpp>
//
namespace oomph { namespace libfabric
{
struct endpoint_wrapper;

// cppcheck-suppress ConfigurationNotChecked
static hpx::debug::enable_print<false> ctx_deb("CONTEXT");

// This struct holds the ready state of a future
// we must also store the context used in libfabric, in case
// a request is cancelled - fi_cancel(...) needs it
struct operation_context
{
private:
    // libfabric requires some space for it's internal bookkeeping
    // so the first member of this struct must be fi_context
    fi_context context_reserved_space;

public:
    using cb_ptr_t = util::detail::unique_function<void>*;
    using lockfree_queue = boost::lockfree::queue<cb_ptr_t,
        boost::lockfree::fixed_sized<false>, boost::lockfree::allocator<std::allocator<void>>>;
    //
    cb_ptr_t        user_cb_;
    bool            is_send_;
    lockfree_queue *callback_queue_;

    operation_context(util::unique_function<void(void)> &&user_cb, bool send, lockfree_queue *queue)
      : context_reserved_space()
      , user_cb_(std::move(user_cb).release())
      , is_send_(send)
      , callback_queue_(queue)
    {
    }

    // --------------------------------------------------------------------
    //
    void handle_error(struct fi_cq_err_entry /*e*/)
    {
        std::terminate();
    }

    // --------------------------------------------------------------------
    // Called when a send completes
    int handle_send_completion()
    {
        OOMPH_DP_ONLY(ctx_deb, debug(hpx::debug::str<>("handle_send_completion")
                                    , hpx::debug::ptr(this)));
        // enqueue the callback
        while (!callback_queue_->push(user_cb_)) {}
        return 1;
    }

    // --------------------------------------------------------------------
    // Called when a recv completes
    int handle_recv_completion(std::uint64_t /*len*/)
    {
        OOMPH_DP_ONLY(ctx_deb, debug(hpx::debug::str<>("handle_recv_completion")
                                    , hpx::debug::ptr(this)));
        // enqueue the callback
        while (!callback_queue_->push(user_cb_)) {}
        return 1;
    }

    // --------------------------------------------------------------------
    // when a comlpetion returns FI_ECANCELED, this should be called
    // but in fact we are not honouring this as it is not needed yet.
    bool cancel(endpoint_wrapper* /*endpoint*/)
    {
        // bool ok = (fi_cancel(&endpoint_->get_ep()->fid, this) == 0);
        return false;
    }
};


}} // namespace oomph
