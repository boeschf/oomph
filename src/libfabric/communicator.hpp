/*
 * GridTools
 *
 * Copyright (c) 2014-2021, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#pragma once

#include <cstdint>

#include <oomph/context.hpp>
#include <oomph/communicator.hpp>
//
#include "../communicator_base.hpp"
#include "../device_guard.hpp"
//
#include "./operation_context.hpp"
#include "./controller.hpp"
#include "./context.hpp"

namespace oomph
{

struct detail::request_state::reserved_t
{
    oomph::libfabric::operation_context operation_context_;
};

// cppcheck-suppress ConfigurationNotChecked
static hpx::debug::enable_print<false> com_deb("COMMUNI");
static hpx::debug::enable_print<true>  com_err("COMMUNI");

class communicator_impl : public communicator_base<communicator_impl>
{
    using rank_type = communicator::rank_type;
    using tag_type  = std::uint64_t;
    //
    using segment_type = oomph::libfabric::memory_segment;
    using region_type = segment_type::handle_type;

    using cb_ptr_t = libfabric::operation_context::cb_ptr_t;
    using callback_queue = boost::lockfree::queue<cb_ptr_t,
        boost::lockfree::fixed_sized<false>, boost::lockfree::allocator<std::allocator<void>>>;

  public:
    context_impl       *m_context;
    std::uintptr_t      m_ctag;
    libfabric::endpoint_wrapper m_tx_endpoint;
    libfabric::endpoint_wrapper m_rx_endpoint;
    //
    callback_queue      m_send_cb_queue;
    callback_queue      m_recv_cb_queue;

    communicator_impl(context_impl* ctxt)
    : communicator_base(ctxt)
    , m_context(ctxt)
    , m_ctag(0)
    , m_send_cb_queue(128)
    , m_recv_cb_queue(128)
    {
        OOMPH_DP_ONLY(com_deb, debug(hpx::debug::str<>("MPI_comm"), hpx::debug::ptr(mpi_comm())));
        m_tx_endpoint = m_context->get_controller()->get_tx_endpoint();
        m_rx_endpoint = m_context->get_controller()->get_rx_endpoint();

#ifdef ADD_COMM_ID_TO_TAG

        // this chunk of code might be needed if the same tag is used
        // simultaneously on differnt communicators

        const int random_msg_tag = 65535;
        if (rank()==0) {
            m_ctag = reinterpret_cast<std::uintptr_t>(this);
            OOMPH_DP_ONLY(com_deb, debug(hpx::debug::str<>("MPI send tag")
                                        ,hpx::debug::hex<8>(m_ctag)));
            for (int i=1; i<size(); ++i) {
                MPI_Send(&m_ctag, sizeof(std::uintptr_t), MPI_CHAR, i, random_msg_tag, mpi_comm());
            }
        }
        else {
            MPI_Status status;
            MPI_Recv(&m_ctag, sizeof(std::uintptr_t), MPI_CHAR, 0, random_msg_tag, mpi_comm(), &status);
            OOMPH_DP_ONLY(com_deb, debug(hpx::debug::str<>("MPI recv tag")
                                        ,hpx::debug::hex<8>(m_ctag)));
        }
#endif
    }

    ~communicator_impl()
    {
        clear_callback_queues();
    }

    auto& get_heap() noexcept { return m_context->get_heap(); }

    // --------------------------------------------------------------------
    // generate a tag with 0xaaaaaaRRRRtttttt address, rank, tag info
    inline std::uint64_t make_tag64(std::uint32_t tag, std::uint32_t rank) {
        return (
                ((std::uint64_t(m_ctag) & 0x0000000000FFFFFF) << 40) |
                ((std::uint64_t(rank)   & 0x000000000000FFFF) << 24) |
                ((std::uint64_t(tag)    & 0x0000000000FFFFFF))
               );
    }

    // --------------------------------------------------------------------
    // this takes a pinned memory region and sends it
    void send_tagged_region(region_type const &send_region, fi_addr_t dst_addr_, uint64_t tag_, void *ctxt)
    {
        [[maybe_unused]] auto scp = com_deb.scope(hpx::debug::ptr(this), __func__);
        OOMPH_DP_ONLY(com_deb, debug(hpx::debug::str<>("send message buffer")
                                    , "->", hpx::debug::dec<2>(dst_addr_)
                                    , send_region
                                    , "tag", hpx::debug::hex<16>(tag_)
                                    , "context", hpx::debug::ptr(ctxt)
                                    , "endpoint", hpx::debug::ptr(m_tx_endpoint.get_ep())));

        bool ok = false;
        while (!ok) {
            ssize_t ret;
            ret = fi_tsend(m_tx_endpoint.get_ep(),
                           send_region.get_address(),
                           send_region.get_size(),
                           send_region.get_local_key(),
                           dst_addr_, tag_, ctxt);
            OOMPH_DP_ONLY(com_deb, debug(hpx::debug::str<>("fi_tsend")
                                        , "tx endpoint", hpx::debug::ptr(m_tx_endpoint.get_ep())));
            if (ret == 0) {
//                ++m_shared_state->m_controller->sends_posted_;
                ok = true;
            }
            else if (ret == -FI_EAGAIN) {
//                com_deb.error("Reposting fi_sendv / fi_tsendv");
                // no point stressing the system
                progress();
            }
            else if (ret == -FI_ENOENT) {
                // if a node has failed, we can recover
                // @TODO : put something better here
                com_deb.error("No destination endpoint, terminating.");
                std::terminate();
            }
            else if (ret)
            {
                throw libfabric::fabric_error(int(ret), "fi_sendv / fi_tsendv");
            }
        }
    }

    // --------------------------------------------------------------------
    // the receiver posts a single receive buffer to the queue, attaching
    // itself as the context, so that when a message is received
    // the owning receiver is called to handle processing of the buffer
    void recv_tagged_region(region_type const &recv_region, fi_addr_t src_addr_, uint64_t tag_, void *ctxt)
    {
        [[maybe_unused]] auto scp = com_deb.scope(hpx::debug::ptr(this), __func__);

        OOMPH_DP_ONLY(com_deb, debug(hpx::debug::str<>("recv message buffer")
                                    , "<-", hpx::debug::dec<2>(src_addr_)
                                    , recv_region
                                    , "tag", hpx::debug::hex<16>(tag_)
                                    , "context", hpx::debug::ptr(ctxt)
                                    , "rx endpoint", hpx::debug::ptr(m_rx_endpoint.get_ep())));

        auto rx_ep = m_rx_endpoint.get_ep();

        // this should never actually return true and yield/sleep
        bool ok = false;
        while (!ok) {
            uint64_t ignore = 0;
//            OOMPH_DP_ONLY(com_deb, debug(hpx::debug::str<>("fi_trecv")
//                                        , "rx endpoint", hpx::debug::ptr(m_rx_endpoint.get_ep())));
            ssize_t ret = fi_trecv(rx_ep,
                recv_region.get_address(),
                recv_region.get_size(),
                recv_region.get_local_key(),
                FI_ADDR_UNSPEC, tag_, ignore, ctxt);
            OOMPH_DP_ONLY(com_deb, debug(hpx::debug::str<>("fi_trecv")
                                        , "rx endpoint", hpx::debug::ptr(m_rx_endpoint.get_ep())));
            if (ret ==0) {
                //++m_shared_state->m_controller->recvs_posted_;
                ok = true;
            }
            else if (ret == -FI_EAGAIN)
            {
                com_deb.error("reposting fi_trecv\n");
                // no point stressing the system
                progress();
            }
            else if (ret != 0)
            {
                throw libfabric::fabric_error(int(ret), "fi_trecv");
            }
        }
    }

    // --------------------------------------------------------------------
    void send(context_impl::heap_type::pointer const& ptr, std::size_t /*size*/, rank_type dst,
        tag_type tag, util::unique_function<void()>&& cb, communicator::shared_request_ptr&& req)
    {
        [[maybe_unused]] auto scp = com_deb.scope(hpx::debug::ptr(this), __func__, "req", hpx::debug::ptr(&req), "ctx", hpx::debug::ptr(&req->reserved()->operation_context_));
        std::uint64_t stag = make_tag64(tag, this->rank());

        auto &reg = ptr.handle_ref();

        libfabric::operation_context::cb_ptr_t cb_ptr = std::move(cb).release();
        libfabric::operation_context *op_ctx = new (&req->reserved()->operation_context_) libfabric::operation_context(cb_ptr, &m_send_cb_queue);
        assert(reinterpret_cast<void*>(op_ctx) == reinterpret_cast<void*>(&req->reserved()->operation_context_));

        OOMPH_DP_ONLY(com_deb, debug(hpx::debug::str<>("Send")
            , "thisrank", hpx::debug::dec<>(rank())
            , "rank", hpx::debug::dec<>(dst)
            , "tag", hpx::debug::hex<16>(std::uint64_t(tag))
            , "ctag", hpx::debug::hex<8>(m_ctag)
            , "stag", hpx::debug::hex<16>(stag)
            , "addr", hpx::debug::ptr(reg.get_address())
            , "size", hpx::debug::hex<6>(reg.get_size())
            , "op_ctx", hpx::debug::ptr(op_ctx)));

        // com_deb.trace(debug::mem_crc32(reg.get_address(), size, "send"));

        // async send of the region
        send_tagged_region(reg, fi_addr_t(dst), stag, op_ctx);
    }

    void recv(context_impl::heap_type::pointer& ptr, std::size_t /*size*/, rank_type src, tag_type tag,
        util::unique_function<void()>&& cb, communicator::shared_request_ptr&& req)
    {
        [[maybe_unused]] auto scp = com_deb.scope(hpx::debug::ptr(this), __func__, "req", hpx::debug::ptr(&req), "ctx", hpx::debug::ptr(&req->reserved()->operation_context_));
        std::uint64_t stag = make_tag64(tag, src);

        auto &reg = ptr.handle_ref();

        libfabric::operation_context::cb_ptr_t cb_ptr = std::move(cb).release();
        libfabric::operation_context *op_ctx = new (&req->reserved()->operation_context_) libfabric::operation_context(cb_ptr, &m_recv_cb_queue);
        assert(reinterpret_cast<void*>(op_ctx) == reinterpret_cast<void*>(&req->reserved()->operation_context_));

        OOMPH_DP_ONLY(com_deb, debug(hpx::debug::str<>("Recv")
            , "thisrank", hpx::debug::dec<>(rank())
            , "rank", hpx::debug::dec<>(src)
            , "tag",  hpx::debug::hex<16>(std::uint64_t(tag))
            , "ctag", hpx::debug::hex<8>(m_ctag)
            , "stag", hpx::debug::hex<16>(stag)
            , "addr", hpx::debug::ptr(reg.get_address())
            , "size", hpx::debug::hex<6>(reg.get_size())
            , "op_ctx", hpx::debug::ptr(op_ctx)));

        // async recv of the region
        recv_tagged_region(reg, fi_addr_t(src), stag, op_ctx);
    }

    void progress()
    {
        // get main libfabric controller
        {
            bool retry;
            do
            {
                int tsend = m_context->get_controller()->poll_send_queue(m_tx_endpoint.get_tx_cq());
                int trecv = m_context->get_controller()->poll_recv_queue(m_rx_endpoint.get_rx_cq());
                // retry until no new completion events are found.
                // This helps progress all messages
                retry = (tsend | trecv) != 0;
            } while (retry);
        }

        clear_callback_queues();
    }

    void clear_callback_queues()
    {
        // work through ready callbacks, which were pushed to the queue
        // (by other threads)
        m_send_cb_queue.consume_all([](cb_ptr_t user_cb_) {
            [[maybe_unused]] auto scp = com_deb.scope("m_send_cb_queue.consume_all", user_cb_);
            user_cb_->invoke();
            delete user_cb_;
            const void *ptr = (void*)(0x1111111111111111);
            user_cb_ = (cb_ptr_t)ptr;
        });

        m_recv_cb_queue.consume_all([](cb_ptr_t user_cb_) {
            [[maybe_unused]] auto scp = com_deb.scope("m_recv_cb_queue.consume_all", user_cb_);
            user_cb_->invoke();
            delete user_cb_;
            const void *ptr = (void*)(0x2222222222222222);
            user_cb_ = (cb_ptr_t)ptr;
        });
    }

    // Cancel is a problem with libfabric because fi_cancel is asynchronous.
    // The item to be cancelled will either complete with CANCELLED status
    // or will complete as usual (ie before the cancel could take effect)
    //
    // We can only be certain if we poll until the completion happens
    // or attach a callback to the cancel notification which is not supported
    // by oomph.
    bool cancel_recv_cb(recv_request const& req)
    {
        // get the original message operation context
        libfabric::operation_context *op_ctx = reinterpret_cast<libfabric::operation_context*>(&req.m_data->reserved()->operation_context_);

        // replace the callback in the original message context with a cancel one
        bool found = false;
        util::unique_function<void(void)> temp = [&](){ found=true; };
        auto orig_cb = std::exchange(op_ctx->user_cb_, temp.release());

        // submit the cancellation request
        bool ok = (fi_cancel(&m_rx_endpoint.get_ep()->fid, op_ctx) == 0);
        OOMPH_DP_ONLY(com_deb, debug(hpx::debug::str<>("Cancel")
            , "ok", ok
            , "op_ctx", hpx::debug::ptr(op_ctx)));

        // if the cancellation request is ok, poll until we know if it worked
        // there is an implicit race here as another thread might get the completion
        if (ok) {
            // swap the callback with ours
            // now poll until the callback is triggered
            while (!found) {
                // poll receives (sends can't be cancelled)
                const void *ptr = (void*)(0xffffffffffffffff);
                m_context->get_controller()->poll_recv_queue(m_rx_endpoint.get_rx_cq());

                if (reinterpret_cast<void*>(op_ctx->user_cb_) == ptr) {
                    found = true;
                    OOMPH_DP_ONLY(com_err, error(hpx::debug::str<>("Cancel")
                        , "ERROR"
                        , "op_ctx", hpx::debug::ptr(op_ctx)));
                }
                if (op_ctx->user_cb_ == nullptr) {
                    found = true;
//                    OOMPH_DP_ONLY(com_err, error(hpx::debug::str<>("Cancel")
//                        , "NULL ERROR"
//                        , "op_ctx", hpx::debug::ptr(op_ctx)));
                }
            }
//            OOMPH_DP_ONLY(com_err, error(hpx::debug::str<>("Cancel")
//                , "Triggered"
//                , "op_ctx", hpx::debug::ptr(op_ctx)));
            delete orig_cb;
            return true;
        }
        return ok;
    }
};

} // namespace oomph
