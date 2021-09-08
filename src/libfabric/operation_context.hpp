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
    lockfree_queue *callback_queue_;

    operation_context(cb_ptr_t user_cb, lockfree_queue *queue)
      : context_reserved_space()
      , user_cb_(user_cb)
      , callback_queue_(queue)
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
    // we only need to delete the callback, not invoke it
    void handle_cancelled()
    {
        [[maybe_unused]] auto scp = ctx_deb.scope(hpx::debug::ptr(this), __func__, user_cb_);
        user_cb_->invoke();
        delete user_cb_;
        const void *ptr = (void*)(0x0000000000000000);
        user_cb_ = (cb_ptr_t)ptr;
    }

    // --------------------------------------------------------------------
    // Called when a recv completes
    int handle_recv_completion(bool threadlocal)
    {
        [[maybe_unused]] auto scp = ctx_deb.scope(hpx::debug::ptr(this), __func__, user_cb_);
        if (!threadlocal) {
            // enqueue the callback
            while (!callback_queue_->push(user_cb_)) {}
        }
        else {
            if (user_cb_ == reinterpret_cast<decltype(user_cb_)>(0xffffffffffffffff))
            {
                ctx_deb.error(hpx::debug::ptr(this), __func__, user_cb_);
                throw std::runtime_error("Invalid callback");
            }
            user_cb_->invoke();
            delete user_cb_;
            // mark this callback as invalid
            user_cb_ = reinterpret_cast<decltype(user_cb_)>(0xffffffffffffffff);
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
            while (!callback_queue_->push(user_cb_)) {}
        }
        else {
            user_cb_->invoke();
            delete user_cb_;
            const void *ptr = (void*)(0x3333333333333333);
            user_cb_ = (cb_ptr_t)ptr;
        }
        return 1;
    }
};

}} // namespace oomph
