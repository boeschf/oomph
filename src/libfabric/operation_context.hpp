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
#include <oomph/request.hpp>
//
#include <boost/lockfree/queue.hpp>
//
namespace oomph { namespace libfabric
{
// cppcheck-suppress ConfigurationNotChecked
static hpx::debug::enable_print<false> ctx_deb("CONTEXT");

struct endpoint_wrapper;
struct operation_context;


struct queue_data
{
    using cb_ptr_t = util::detail::unique_function<void>*;
    //
    operation_context *ctxt;
    cb_ptr_t           user_cb_;
};

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
    using lockfree_queue = boost::lockfree::queue<queue_data,
        boost::lockfree::fixed_sized<false>, boost::lockfree::allocator<std::allocator<void>>>;
    //
    cb_ptr_t        user_cb_;
    lockfree_queue *callback_queue_;
    lockfree_queue *cancel_queue_;

    operation_context(cb_ptr_t user_cb, lockfree_queue *queue, lockfree_queue *cancelq)
      : context_reserved_space()
      , user_cb_(user_cb)
      , callback_queue_(queue)
      , cancel_queue_(cancelq)
    {
        [[maybe_unused]] auto scp = ctx_deb.scope(hpx::debug::ptr(this), __func__, user_cb_);
    }

    ~operation_context()
    {
        delete user_cb_;
    }

    // --------------------------------------------------------------------
    //
    void handle_error(struct fi_cq_err_entry /*e*/)
    {
        std::terminate();
    }

    // --------------------------------------------------------------------
    // When a completion returns FI_ECANCELED, this is called
    void handle_cancelled()
    {
        [[maybe_unused]] auto scp = ctx_deb.scope(hpx::debug::ptr(this), __func__, user_cb_);
        // enqueue the cancelled/callback
        while (!cancel_queue_->push(queue_data{this,user_cb_})) {}
    }

    // --------------------------------------------------------------------
    // Called when a recv completes
    int handle_recv_completion(bool threadlocal)
    {
        [[maybe_unused]] auto scp = ctx_deb.scope(hpx::debug::ptr(this), __func__, user_cb_);
        if (!threadlocal) {
            // enqueue the callback
            while (!callback_queue_->push(queue_data{this,user_cb_})) {}
        }
        else {
            user_cb_->invoke();
            delete user_cb_;
        }
        return 1;
    }

    // --------------------------------------------------------------------
    // Called when a send completes
    int handle_send_completion(bool threadlocal)
    {
        [[maybe_unused]] auto scp = ctx_deb.scope(hpx::debug::ptr(this), __func__, user_cb_);
        if (!threadlocal) {
            // enqueue the callback
            while (!callback_queue_->push(queue_data{this,user_cb_})) {}
        }
        else {
            user_cb_->invoke();
            delete user_cb_;
        }
        return 1;
    }
};

}} // namespace oomph
