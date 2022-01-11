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
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
// ??
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
//
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_tagged.h>
//
#include "./fabric_error.hpp"
#include "./locality.hpp"
#include "./performance_counter.hpp"
#include "./print.hpp"
#include "./memory_region.hpp"
#include "./operation_context.hpp"
//
#include <oomph/util/unique_function.hpp>
//
#include "oomph_libfabric_defines.hpp"
//
#include <mpi.h>

// ----------------------------------------
// auto progress (libfabric thread) or manual
// ----------------------------------------
fi_progress libfabric_progress_type()
{
#if defined(OOMPH_LIBFABRIC_SOCKETS) || defined(OOMPH_LIBFABRIC_TCP)
    return FI_PROGRESS_AUTO;
#else
    if (std::getenv("LIBFABRIC_AUTO_PROGRESS") == nullptr)
        return FI_PROGRESS_MANUAL;
    return FI_PROGRESS_AUTO;
#endif
}

std::string libfabric_progress_string()
{
    if (libfabric_progress_type() == FI_PROGRESS_AUTO)
        return "auto";
    return "manual";
}

#ifndef LIBFABRIC_PROGRESS_STRING
#define LIBFABRIC_PROGRESS_TYPE libfabric_progress_type()
#define LIBFABRIC_PROGRESS_STRING libfabric_progress_string()
#endif

// ----------------------------------------
// shared endpoint or separate for send/recv
// ----------------------------------------
int libfabric_endpoint_type()
{
    auto lf_ep_type = std::getenv("LIBFABRIC_ENDPOINT_TYPE");
    if (lf_ep_type == nullptr)
        return 0;
    if (std::string(lf_ep_type) == std::string("multiple") ||
        std::atoi(lf_ep_type) == 1)
        return 1;
    if (std::string(lf_ep_type) == std::string("threadlocal") ||
        std::atoi(lf_ep_type) == 2)
        return 2;
    if (std::string(lf_ep_type) == std::string("scalable") ||
        std::atoi(lf_ep_type) == 3)
        return 3;
    return 0;
}

std::string libfabric_endpoint_string()
{
    auto lf_ep_type = libfabric_endpoint_type();
    if (lf_ep_type == 1)
        return "multiple";
    if (lf_ep_type == 2)
        return "threadlocal";
    if (lf_ep_type == 3)
        return "scalable";
    return "single";
}

#ifndef LIBFABRIC_ENDPOINT_STRING
#define LIBFABRIC_ENDPOINT_STRING libfabric_endpoint_string()
#endif

// ------------------------------------------------
// Needed on Cray for GNI extensions
// ------------------------------------------------
#ifdef OOMPH_LIBFABRIC_GNI
#include "rdma/fi_ext_gni.h"
#endif

#ifdef OOMPH_LIBFABRIC_HAVE_PMI
#include <pmi2.h>
#endif

#define LIBFABRIC_FI_VERSION_MAJOR 1
#define LIBFABRIC_FI_VERSION_MINOR 11

using namespace hpx;

namespace oomph {
    // cppcheck-suppress ConfigurationNotChecked
    static debug::enable_print<false> cnt_deb("CONTROL");
    static debug::enable_print<true>  cnt_err("CONTROL");
}

/** @brief a class to return the number of progressed callbacks */
struct progress_status {
    int m_num_sends = 0;
    int m_num_recvs = 0;
    int m_num_cancels = 0;

    int num() const noexcept { return m_num_sends+m_num_recvs+m_num_cancels; }
    int num_sends() const noexcept { return m_num_sends; }
    int num_recvs() const noexcept { return m_num_recvs; }
    int num_cancels() const noexcept { return m_num_cancels; }

    progress_status& operator+=(const progress_status& other) noexcept {
        m_num_sends += other.m_num_sends;
        m_num_recvs += other.m_num_recvs;
        m_num_cancels += other.m_num_cancels;
        return *this;
    }
};

namespace oomph { namespace libfabric {

    using region_type = oomph::libfabric::memory_handle;
    using endpoint_context_pool = boost::lockfree::queue<endpoint_wrapper, boost::lockfree::fixed_sized<false>>;

    template <typename Handle>
    void fidclose(Handle fid, const char *msg)
    {
        OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("closing"), msg));
        int ret = fi_close(fid);
        if (ret == -FI_EBUSY) {
            throw fabric_error(ret, "fi_close EBUSY");
        }
        else if (ret == FI_SUCCESS) {
            return;
        }
        throw fabric_error(ret, "fi_close error");
    }

    // when using thread local endpoints, we encapsulate things that
    // are needed to manage an endpoint
    struct endpoint_wrapper
    {
    private:
        fid_ep* ep_ = nullptr;
        fid_cq* rq_ = nullptr;
        fid_cq* tq_ = nullptr;
        const char *name_ = nullptr;

    public:
        endpoint_wrapper() {}
        endpoint_wrapper(fid_ep* ep, fid_cq* rq, fid_cq* tq, const char *name)
            : ep_(ep)
            , rq_(rq)
            , tq_(tq)
            , name_(name)
        {
            [[maybe_unused]] auto scp =
                oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__, name_);
        }

        // to keep boost::lockfree happy, we need these
        endpoint_wrapper(const endpoint_wrapper &ep) = default;
        endpoint_wrapper & operator = (const endpoint_wrapper &ep) = default;

        void cleanup()
        {
            [[maybe_unused]] auto scp =
                oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__, name_);
            if (ep_) {
                fidclose(&ep_->fid, "endpoint");
                ep_ = nullptr;
            }
            if (rq_) {
                fidclose(&rq_->fid, "RQ");
                rq_ = nullptr;
            }
            if (tq_) {
                fidclose(&tq_->fid, "TQ");
                tq_ = nullptr;
            }
        }

        inline fid_ep* get_ep()
        {
            return ep_;
        }
        inline fid_cq* get_rx_cq()
        {
            return rq_;
        }
        inline fid_cq* get_tx_cq()
        {
            return tq_;
        }
        inline const char* get_name()
        {
            return name_;
        }

    };

    struct stack_endpoint {
        endpoint_wrapper       endpoint_;
        endpoint_context_pool *pool_;
        //
        stack_endpoint()
            : endpoint_()
            , pool_(nullptr) {}
        //
        stack_endpoint(fid_ep* ep, fid_cq* rq, fid_cq* tq, const char *name, endpoint_context_pool *pool)
            : endpoint_(ep, rq, tq, name)
            , pool_(pool) {}
        //
        stack_endpoint & operator = (stack_endpoint &&other) {
            endpoint_ = std::move(other.endpoint_);
            pool_ = std::exchange(other.pool_, nullptr);
            return *this;
        }

        ~stack_endpoint() {
            if (!pool_) return;
            OOMPH_DP_ONLY(cnt_deb,
                trace(debug::str<>("Scalable Ep"), "used push"
                      , "ep", hpx::debug::ptr(get_ep())
                      , "tx cq", hpx::debug::ptr(get_tx_cq())
                      , "rx cq", hpx::debug::ptr(get_rx_cq())));
            pool_->push(endpoint_);
        }

        inline fid_ep* get_ep()
        {
            return endpoint_.get_ep();
        }

        inline fid_cq* get_rx_cq()
        {
            return endpoint_.get_rx_cq();
        }

        inline fid_cq* get_tx_cq() {
            return endpoint_.get_tx_cq();
        }
    };

    struct endpoints_lifetime_manager {
        // threadlocal endpoints
        static inline thread_local stack_endpoint tl_tx_;
        static inline thread_local stack_endpoint tl_stx_;
        static inline thread_local stack_endpoint tl_srx_;
        // non threadlocal endpoints, tx/rx
        endpoint_wrapper ep_tx_;
        endpoint_wrapper ep_rx_;
    };

    // struct returned from polling functions
    // if any completions are handled (rma, send, recv),
    // then the completions_handled field should be set to true.
    // if Send/Receive completions that indicate the end of a message that
    // should trigger a future or callback in user level code occur, then
    // the user_msgs field should hold the count (1 per future/callback)
    // A non zero user_msgs count implies completions_handled must be set
    struct progress_count
    {
        bool completions_handled = false;
        std::uint32_t user_msgs = 0;
        //
        progress_count& operator+=(const progress_count& rhs)
        {
            completions_handled |= rhs.completions_handled;
            user_msgs += rhs.user_msgs;
            return *this;
        }
    };

    enum class endpoint_type : int
    {
        single = 0,
        multiple = 1,
        threadlocal = 2,
        scalable = 3,
    };

    class controller
    {
    public:
        typedef std::mutex mutex_type;
        typedef std::lock_guard<mutex_type> scoped_lock;

    private:
        // For threadlocal/scalable endpoints,
        // we use a dedicated threadlocal endpoint wrapper
        // NB. inline static requires c++17
        std::unique_ptr<endpoints_lifetime_manager> eps_;

        using endpoint_context_pool = boost::lockfree::queue<endpoint_wrapper, boost::lockfree::fixed_sized<false>>;
        endpoint_context_pool tx_endpoints_;
        endpoint_context_pool rx_endpoints_;

        struct fi_info* fabric_info_;
        struct fid_fabric* fabric_;
        struct fid_domain* fabric_domain_;
        struct fid_pep* ep_passive_;

        struct fid_av* av_;
        endpoint_type endpoint_type_;

        locality here_;
        locality root_;

        // used during queue creation setup and during polling
        mutex_type controller_mutex_;
        mutex_type send_mutex_;
        mutex_type recv_mutex_;

    public:
        //
        performance_counter<int, false> sends_posted_;
        performance_counter<int, false> recvs_posted_;
        performance_counter<int, false> sends_readied_;
        performance_counter<int, false> recvs_readied_;
        performance_counter<int, false> sends_complete;
        performance_counter<int, false> recvs_complete;

        void finvoke(const char *msg, const char *err, int ret) {
            OOMPH_DP_ONLY(cnt_deb, trace(debug::str<>(msg)));
            if (ret) throw fabric_error(ret, err);
        }

    public:
        // --------------------------------------------------------------------
        // constructor gets info from device and sets up all necessary
        // maps, queues and server endpoint etc
        controller(/*oomph::context_impl* ctx, */std::string const& provider, std::string const& domain,
            MPI_Comm mpi_comm, int rank, int size, size_t threads)
          : eps_(nullptr)
          , tx_endpoints_(1)
          , rx_endpoints_(1)
          , fabric_info_(nullptr)
          , fabric_(nullptr)
          , fabric_domain_(nullptr)
          , ep_passive_(nullptr)
          , av_(nullptr)
          , sends_posted_(0)
          , recvs_posted_(0)
          , sends_readied_(0)
          , recvs_readied_(0)
          , sends_complete(0)
          , recvs_complete(0)
        {
            OOMPH_DP_ONLY(
                cnt_deb, eval([]() { std::cout.setf(std::ios::unitbuf); }));
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);

            endpoint_type_ =
                static_cast<endpoint_type>(libfabric_endpoint_type());
            OOMPH_DP_ONLY(cnt_deb,
                debug(debug::str<>("Endpoints"), LIBFABRIC_ENDPOINT_STRING));

            eps_ = std::make_unique<endpoints_lifetime_manager>();

            OOMPH_DP_ONLY(cnt_deb,
                debug(debug::str<>("Threads"), debug::dec<3>(threads)));

            open_fabric(provider, domain, threads, rank == 0);

            // if we are using scalable endpoints, then setup tx/rx contexts
            // we will us a single endpoint for all Tx/Rx contexts
            if (endpoint_type_ == endpoint_type::scalable)
            {
                // create an address vector that will be bound to endpoints
                av_ = create_address_vector(fabric_info_, size, threads);
                OOMPH_DP_ONLY(cnt_deb,
                    debug(debug::str<>("Created AV"), hpx::debug::ptr(av_)));

                // thread slots might not be same as what we asked for
                size_t threads_allocated = 0;
                auto ep_sx = new_endpoint_scalable(
                    fabric_domain_, fabric_info_, threads, threads_allocated);
                if (!ep_sx)
                    throw fabric_error(FI_EOTHER, "fi_scalable endpoint creation failed");

                OOMPH_DP_ONLY(cnt_deb,
                    trace(debug::str<>("scalable endpoint ok"),
                        "Contexts allocated", debug::dec<4>(threads_allocated)));

                // prepare the stack for insertions
                tx_endpoints_.reserve(threads_allocated);
                rx_endpoints_.reserve(threads_allocated);
                //
                for (unsigned int i = 0; i < threads_allocated; i++)
                {

                    // For threadlocal/scalable endpoints, tx/rx resources
                    fid_ep *scalable_ep_tx;
                    fid_cq *scalable_cq_tx;
                    fid_ep *scalable_ep_rx;
                    fid_cq *scalable_cq_rx;

                    // Tx context setup
                    finvoke("create tx context", "fi_tx_context", fi_tx_context(
                        ep_sx, i, NULL, &scalable_ep_tx, NULL));

                    OOMPH_DP_ONLY(cnt_deb,
                        trace(debug::str<>("create CQ"), "tx", debug::dec<4>(i)));
                    scalable_cq_tx = create_completion_queue(
                        fabric_domain_, fabric_info_->tx_attr->size);

                    OOMPH_DP_ONLY(cnt_deb,
                        trace(debug::str<>("fi_scalable_ep_bind"), "tx", debug::dec<4>(i)));
                    bind_queue_to_endpoint(scalable_ep_tx, scalable_cq_tx, FI_TRANSMIT);

                    OOMPH_DP_ONLY(cnt_deb,
                        trace(debug::str<>("enable_endpoint"), "tx", debug::dec<4>(i)));
                    enable_endpoint(scalable_ep_tx);

                    endpoint_wrapper tx(scalable_ep_tx, nullptr, scalable_cq_tx, "Tx scalable");
                    OOMPH_DP_ONLY(cnt_deb,
                                  trace(debug::str<>("Scalable Ep"), "initial tx push"
                                        , "ep", hpx::debug::ptr(tx.get_ep())
                                        , "tx cq", hpx::debug::ptr(tx.get_tx_cq())
                                        , "rx cq", hpx::debug::ptr(tx.get_rx_cq())));
                    tx_endpoints_.push(tx);

                    // Rx contexts
                    finvoke("create rx context", "fi_rx_context", fi_rx_context(
                        ep_sx, i, NULL, &scalable_ep_rx, NULL));

                    OOMPH_DP_ONLY(cnt_deb,
                        trace(debug::str<>("create CQ"), "rx", debug::dec<4>(i)));
                    scalable_cq_rx = create_completion_queue(
                        fabric_domain_, fabric_info_->rx_attr->size);

                    OOMPH_DP_ONLY(cnt_deb,
                        trace(debug::str<>("fi_scalable_ep_bind"), "rx", debug::dec<4>(i)));
                    bind_queue_to_endpoint(scalable_ep_rx, scalable_cq_rx, FI_RECV);
                    OOMPH_DP_ONLY(cnt_deb,
                        trace(debug::str<>("enable_endpoint"), "rx", debug::dec<4>(i)));
                    enable_endpoint(scalable_ep_rx);

                    endpoint_wrapper rx(scalable_ep_rx, scalable_cq_rx, nullptr, "Rx scalable");
                    OOMPH_DP_ONLY(cnt_deb,
                                  trace(debug::str<>("Scalable Ep"), "initial rx push"
                                        , "ep", hpx::debug::ptr(rx.get_ep())
                                        , "tx cq", hpx::debug::ptr(rx.get_tx_cq())
                                        , "rx cq", hpx::debug::ptr(rx.get_rx_cq())));
                    rx_endpoints_.push(rx);
                }

                finvoke("fi_scalable_ep_bind AV", "fi_scalable_ep_bind", fi_scalable_ep_bind(ep_sx, &av_->fid, 0));

                eps_->ep_rx_ =
                    endpoint_wrapper(ep_sx, nullptr, nullptr, "RX scalable");
            }
            else
            {
                // create an address vector that will be bound to endpoints
                av_ = create_address_vector(fabric_info_, size, 0);
                OOMPH_DP_ONLY(cnt_deb,
                    debug(debug::str<>("Created AV"), hpx::debug::ptr(av_)));

                // setup an endpoint for receiving messages
                // rx endpoint is shared by all threads
                here_ = locality("127.0.0.1", "7909");
                auto ep_rx = new_endpoint_active(
                    fabric_domain_, fabric_info_, here_.fabric_data(), rank == 0);

                // bind address vector
                bind_address_vector_to_endpoint(ep_rx, av_);

                // create a completion queue for the rx endpoint
                fabric_info_->rx_attr->op_flags |= FI_COMPLETION;
                auto rx_cq = create_completion_queue(
                    fabric_domain_, fabric_info_->rx_attr->size);

                // bind CQ to endpoint
                bind_queue_to_endpoint(ep_rx, rx_cq, FI_RECV);

#if defined(OOMPH_LIBFABRIC_SOCKETS) || defined(OOMPH_LIBFABRIC_TCP) || defined(OOMPH_LIBFABRIC_VERBS)
                // it appears that the rx endpoint cannot be enabled if it does not
                // have a Tx CQ (at least when using sockets), so we create a dummy
                // Tx CQ and bind it just to stop libfabric from triggering an error.
                // The tx_cq won't actually be used because the call to
                // get endpoint will return another endpoint with the correct
                // cq bound to it
                bool fix_rx_enable_bug = true;
#else
                bool fix_rx_enable_bug = false;
#endif
                if (endpoint_type_ == endpoint_type::single)
                {
                    // bind a tx cq to the rx endpoint for single endpoint type
                    // we need this with or without the bug mentioned above
                    auto tx_cq = bind_tx_queue_to_rx_endpoint(ep_rx, true);
                    eps_->ep_rx_ = endpoint_wrapper(ep_rx, rx_cq, tx_cq, "RX single");
                }
                else if (endpoint_type_ == endpoint_type::multiple)
                {
                    // libfabric sockets bug fix
                    auto a_cq = bind_tx_queue_to_rx_endpoint(ep_rx, fix_rx_enable_bug);
                    eps_->ep_rx_ = endpoint_wrapper(ep_rx, rx_cq, a_cq, "RX multiple");

                    // create a completion queue for tx endpoint
                    fabric_info_->tx_attr->op_flags |= FI_COMPLETION;
                    auto tx_cq = create_completion_queue(
                        fabric_domain_, fabric_info_->tx_attr->size);

                    // setup an endpoint for sending messages
                    // note that the CQ needs FI_RECV even though its a Tx cq to keep
                    // some providers happy as they trigger an error if an endpoint
                    // has no Rx cq attached (appears to be a progress related bug)
                    auto ep_tx = new_endpoint_active(
                        fabric_domain_, fabric_info_, nullptr, rank == 0);
                    bind_queue_to_endpoint(ep_tx, tx_cq, FI_TRANSMIT | FI_RECV);
                    bind_address_vector_to_endpoint(ep_tx, av_);
                    enable_endpoint(ep_tx);

                    // combine endpoints and CQ into wrapper for convenience
                    eps_->ep_tx_ = endpoint_wrapper(ep_tx, nullptr, tx_cq, "TX multiple");
                }
                else if (endpoint_type_ == endpoint_type::threadlocal)
                {
                    // libfabric sockets bug fix
                    auto a_cq = bind_tx_queue_to_rx_endpoint(ep_rx, fix_rx_enable_bug);
                    eps_->ep_rx_ = endpoint_wrapper(ep_rx, rx_cq, a_cq, "RX threadlocal");
                }
            }
            // once enabled we can get the address
            enable_endpoint(eps_->ep_rx_.get_ep());
            here_ = get_endpoint_address(&eps_->ep_rx_.get_ep()->fid);

            // Broadcast address of all endpoints to all ranks
            // and fill address vector with info
            exchange_addresses(av_, mpi_comm, rank, size);
        }

        // --------------------------------------------------------------------
        // clean up all resources
        ~controller()
        {
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);
            unsigned int messages_handled_ = 0;
            unsigned int rma_reads_ = 0;
            unsigned int recv_deletes_ = 0;

            OOMPH_DP_ONLY(cnt_deb,
                debug(debug::str<>("counters"), "Received messages",
                    debug::dec<>(messages_handled_), "Total reads",
                    debug::dec<>(rma_reads_), "Total deletes",
                    debug::dec<>(recv_deletes_), "deletes error",
                    debug::dec<>(messages_handled_ - recv_deletes_)));

            tx_endpoints_.consume_all([](endpoint_wrapper &ep){
                    ep.cleanup();
            });

            rx_endpoints_.consume_all([](endpoint_wrapper &ep){
                    ep.cleanup();
            });

            // No cleanup threadlocals : done by consume_all cleanup above
            // eps_->tl_tx_.endpoint_.cleanup();
            // eps_->tl_stx_.endpoint_.cleanup();
            // eps_->tl_srx_.endpoint_.cleanup();

            // non threadlocal endpoints, tx/rx
            eps_->ep_tx_.cleanup();
            eps_->ep_rx_.cleanup();

            // Cleanup endpoints
            eps_.reset(nullptr);

            // delete adddress vector
            fidclose(&av_->fid, "Address Vector");

            fidclose(&fabric_domain_->fid, "Domain");

            fidclose(&fabric_->fid, "Fabric");

            // clean up
            OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("freeing fabric_info")));

            fi_freeinfo(fabric_info_);
        }

        // --------------------------------------------------------------------
        // send address to rank 0 and receive array of all localities
        void MPI_exchange_localities(fid_av* av, MPI_Comm comm, int rank, int size)
        {
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);
            std::vector<char> localities(size * locality_defs::array_size, 0);
            //
            if (rank > 0)
            {
                OOMPH_DP_ONLY(cnt_deb,
                    debug(debug::str<>("sending here"), iplocality(here_),
                        "size", locality_defs::array_size));
                /*int err = */ MPI_Send(here_.fabric_data(),
                    locality_defs::array_size, MPI_CHAR,
                    0,    // dst rank
                    0,    // tag
                    comm);

                OOMPH_DP_ONLY(cnt_deb,
                    debug(debug::str<>("receiving all"), "size",
                        locality_defs::array_size));

                MPI_Status status;
                /*err = */ MPI_Recv(localities.data(),
                    size * locality_defs::array_size, MPI_CHAR,
                    0,    // src rank
                    0,    // tag
                    comm, &status);
                OOMPH_DP_ONLY(
                    cnt_deb, debug(debug::str<>("received addresses")));
            }
            else
            {
                OOMPH_DP_ONLY(
                    cnt_deb, debug(debug::str<>("receiving addresses")));
                memcpy(&localities[0], here_.fabric_data(),
                    locality_defs::array_size);
                for (int i = 1; i < size; ++i)
                {
                    OOMPH_DP_ONLY(cnt_deb,
                        debug(debug::str<>("receiving address"),
                            debug::dec<>(i)));
                    MPI_Status status;
                    /*int err = */ MPI_Recv(
                        &localities[i * locality_defs::array_size],
                        size * locality_defs::array_size, MPI_CHAR,
                        i,    // src rank
                        0,    // tag
                        comm, &status);
                    OOMPH_DP_ONLY(cnt_deb,
                        debug(
                            debug::str<>("received address"), debug::dec<>(i)));
                }

                OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("sending all")));
                for (int i = 1; i < size; ++i)
                {
                    OOMPH_DP_ONLY(cnt_deb,
                        debug(debug::str<>("sending to"), debug::dec<>(i)));
                    /*int err = */ MPI_Send(&localities[0],
                        size * locality_defs::array_size, MPI_CHAR,
                        i,    // dst rank
                        0,    // tag
                        comm);
                }
            }

            // all ranks should now have a full localities vector
            OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("populating vector")));
            for (int i = 0; i < size; ++i)
            {
                locality temp;
                int offset = i * locality_defs::array_size;
                memcpy(temp.fabric_data_writable(), &localities[offset],
                    locality_defs::array_size);
                insert_address(av, temp);
            }
        }

        // --------------------------------------------------------------------
        // initialize the basic fabric/domain/name
        void open_fabric(std::string const& provider, std::string const& domain,
            int threads, bool rootnode)
        {
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);

            struct fi_info* fabric_hints_ = fi_allocinfo();
            if (!fabric_hints_)
            {
                throw fabric_error(-1, "Failed to allocate fabric hints");
            }

            OOMPH_DP_ONLY(cnt_deb,
                debug(debug::str<>("Here locality"), iplocality(here_)));

#if defined(OOMPH_LIBFABRIC_SOCKETS) || defined(OOMPH_LIBFABRIC_TCP)
            fabric_hints_->addr_format = FI_SOCKADDR_IN;
//#elif defined(OOMPH_LIBFABRIC_VERBS)
//            fabric_hints_->addr_format = FI_SOCKADDR_IB;
#endif

            fabric_hints_->caps =
                FI_MSG | FI_TAGGED /*| FI_DIRECTED_RECV*/ /*| FI_SOURCE*/;

            fabric_hints_->mode = FI_CONTEXT /*| FI_MR_LOCAL*/;
            if (provider.c_str() == std::string("tcp"))
            {
                fabric_hints_->fabric_attr->prov_name =
                    strdup(std::string(provider + ";ofi_rxm").c_str());
            }
            else if (provider.c_str() == std::string("verbs"))
            {
                fabric_hints_->fabric_attr->prov_name =
                    strdup(std::string(provider + ";ofi_rxm").c_str());
            }
            else
            {
                fabric_hints_->fabric_attr->prov_name =
                    strdup(provider.c_str());
            }
            OOMPH_DP_ONLY(cnt_deb,
                debug(debug::str<>("fabric provider"),
                    fabric_hints_->fabric_attr->prov_name));
            if (domain.size() > 0)
            {
                fabric_hints_->domain_attr->name = strdup(domain.c_str());
                OOMPH_DP_ONLY(cnt_deb,
                    debug(debug::str<>("fabric domain"),
                        fabric_hints_->domain_attr->name));
            }

            // use infiniband type basic registration for now
#ifdef OOMPH_LIBFABRIC_GNI
            fabric_hints_->domain_attr->mr_mode = FI_MR_BASIC;
#else
            fabric_hints_->domain_attr->mr_mode = FI_MR_BASIC;
//            fabric_hints_->domain_attr->mr_mode = FI_MR_SCALABLE;
#endif
            // Disable the use of progress threads
            auto progress = libfabric_progress_type();
            fabric_hints_->domain_attr->control_progress = progress;
            fabric_hints_->domain_attr->data_progress = progress;
            OOMPH_DP_ONLY(cnt_deb,
                debug(debug::str<>("progress"), LIBFABRIC_PROGRESS_STRING));

            if (threads>1) {
                // Enable thread safe mode (Does not work with psm2 provider)
                // fabric_hints_->domain_attr->threading = FI_THREAD_SAFE;
                fabric_hints_->domain_attr->threading = FI_THREAD_FID;
            }
            else {
                // we serialize everything
                fabric_hints_->domain_attr->threading = FI_THREAD_DOMAIN;
            }

            // Enable resource management
            fabric_hints_->domain_attr->resource_mgmt = FI_RM_ENABLED;

            OOMPH_DP_ONLY(
                cnt_deb, debug(debug::str<>("fabric endpoint"), "RDM"));
            fabric_hints_->ep_attr->type = FI_EP_RDM;

            uint64_t flags = 0;
            OOMPH_DP_ONLY(cnt_deb,
                debug(debug::str<>("get fabric info"), "FI_VERSION",
                    debug::dec(LIBFABRIC_FI_VERSION_MAJOR),
                    debug::dec(LIBFABRIC_FI_VERSION_MINOR)));

            int ret = fi_getinfo(FI_VERSION(LIBFABRIC_FI_VERSION_MAJOR,
                                     LIBFABRIC_FI_VERSION_MINOR),
                nullptr, nullptr, flags, fabric_hints_, &fabric_info_);
            if (ret)
                throw fabric_error(ret, "Failed to get fabric info");

            if (rootnode)
            {
                OOMPH_DP_ONLY(cnt_deb,
                    trace(debug::str<>("Fabric info"), "\n",
                        fi_tostr(fabric_info_, FI_TYPE_INFO)));
            }

            bool context = (fabric_hints_->mode & FI_CONTEXT) != 0;
            OOMPH_DP_ONLY(
                cnt_deb, debug(debug::str<>("Requires FI_CONTEXT"), context));

            bool mrlocal = (fabric_hints_->mode & FI_MR_LOCAL) != 0;
            OOMPH_DP_ONLY(
                cnt_deb, debug(debug::str<>("Requires FI_MR_LOCAL"), mrlocal));

            OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("Creating fi_fabric")));
            ret = fi_fabric(fabric_info_->fabric_attr, &fabric_, nullptr);
            if (ret)
                throw fabric_error(ret, "Failed to get fi_fabric");

            // Allocate a domain.
            OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("Allocating domain")));
            ret = fi_domain(fabric_, fabric_info_, &fabric_domain_, nullptr);
            if (ret)
                throw fabric_error(ret, "fi_domain");

#ifdef OOMPH_LIBFABRIC_GNI
            {
                [[maybe_unused]] auto scp =
                    oomph::cnt_deb.scope(hpx::debug::ptr(this), "GNI memory registration block");
/*
#ifdef GHEX_GNI_UDREG
                // option 1)
                // Enable use of udreg instead of internal MR cache
                OOMPH_DP_ONLY(
                    cnt_deb, debug(debug::str<>("setting GNI_MR_CACHE = udreg")));
                ret = _set_check_domain_op_value<char*>(GNI_MR_CACHE, "udreg", "GNI_MR_CACHE");
                if (ret)
                    throw fabric_error(ret, "setting GNI_MR_CACHE = udreg");
#else
                // option 2)
                // Disable memory registration cache completely
                OOMPH_DP_ONLY(
                    cnt_deb, debug(debug::str<>("setting GNI_MR_CACHE = none")));
                ret = _set_check_domain_op_value<char*>(GNI_MR_CACHE, "none", "GNI_MR_CACHE");
                if (ret)
                    throw fabric_error(ret, "setting GNI_MR_CACHE = none");
#endif
*/
                // Experiments showed default value of 2048 too high if
                // launching multiple clients on one node
                int32_t udreg_limit = 1024;
                OOMPH_DP_ONLY(cnt_deb,
                    debug(debug::str<>("setting GNI_MR_UDREG_REG_LIMIT ="),
                        debug::hex<4>(1024)));
                ret = _set_check_domain_op_value<int32_t>(
                    GNI_MR_UDREG_REG_LIMIT, &udreg_limit, "GNI_MR_UDREG_REG_LIMIT");
                if (ret)
                    throw fabric_error(ret, "setting GNI_MR_UDREG_REG_LIMIT");

                int32_t enable = 1;
                OOMPH_DP_ONLY(cnt_deb,
                    debug(debug::str<>("setting GNI_MR_CACHE_LAZY_DEREG")));
                // Enable lazy deregistration in MR cache
                ret = _set_check_domain_op_value<int32_t>(
                    GNI_MR_CACHE_LAZY_DEREG, &enable, "GNI_MR_CACHE_LAZY_DEREG");
            }
#endif
            fi_freeinfo(fabric_hints_);
        }

#ifdef OOMPH_LIBFABRIC_GNI
        // --------------------------------------------------------------------
        // Special GNI extensions to disable memory registration cache

        template <typename T>
        int _set_check_domain_op_value(
            [[maybe_unused]] int op, [[maybe_unused]] const T* value, const char *info)
        {
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);
            struct fi_gni_ops_domain* gni_domain_ops;

            int ret = fi_open_ops(&fabric_domain_->fid, FI_GNI_DOMAIN_OPS_1, 0,
                (void**) &gni_domain_ops, nullptr);

            OOMPH_DP_ONLY(cnt_deb,
                debug(debug::str<>("gni open ops"), (ret==0 ? "OK" : "FAIL"), hpx::debug::ptr(gni_domain_ops)));

            // if open was ok, then set value
            if (ret==0) {
                ret = gni_domain_ops->set_val(
                    &fabric_domain_->fid, (dom_ops_val_t) (op), const_cast<T*>(value));

                OOMPH_DP_ONLY(cnt_deb,
                    debug(debug::str<>("gni set ops val"), (ret==0 ? "OK" : "FAIL")));
            }

            // check that the value we set is now returned by get
            T new_value;
            ret = gni_domain_ops->get_val(
                &fabric_domain_->fid, (dom_ops_val_t) (op), &new_value);
            OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("gni op set"), info, new_value));
            //
            return ret;
        }
#endif

        // --------------------------------------------------------------------
        struct fid_ep* new_endpoint_active(struct fid_domain* domain,
            struct fi_info* info, void const* /*src_addr*/, bool /*rootnode*/)
        {
            // don't allow multiple threads to call endpoint create at the same time
            scoped_lock lock(controller_mutex_);

            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);
            OOMPH_DP_ONLY(cnt_deb,
                debug(debug::str<>("Got info mode"),
                    (info->mode & FI_NOTIFY_FLAGS_ONLY)));
/*
            OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("fi_dupinfo")));
            struct fi_info* hints = fi_dupinfo(info);
            if (!hints)
                throw fabric_error(0, "fi_dupinfo");

#if defined(OOMPH_LIBFABRIC_SOCKETS) || defined(OOMPH_LIBFABRIC_TCP)
            if (rootnode && src_addr)
            {
                OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("Allocating"), "socket_data"));
                struct sockaddr_in* socket_data =
                    (struct sockaddr_in*) malloc(sizeof(struct sockaddr_in));
                memcpy(socket_data, src_addr, locality_defs::array_size);
                hints->addr_format = FI_SOCKADDR_IN;
                hints->src_addr = socket_data;
                hints->src_addrlen = sizeof(struct sockaddr_in);
            }
#endif
*/

/*
            int flags = 0;
            struct fi_info* new_hints = nullptr;
            int ret = fi_getinfo(FI_VERSION(LIBFABRIC_FI_VERSION_MAJOR,
                                     LIBFABRIC_FI_VERSION_MINOR),
                nullptr, nullptr, flags, hints, &new_hints);
            if (ret)
                throw fabric_error(ret, "fi_getinfo");
*/
            // If we are the root node, then create connection with the right port address
            //            if (rank == 0) {
            //                OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("root locality = src")
            //                              , iplocality(root_)));
            //                fabric_hints_->src_addr     = socket_data;
            //                fabric_hints_->src_addrlen  = sizeof(struct sockaddr_in);
            //            }

            //            if (src_addr) {
            //                struct sockaddr_in *socket_data = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
            //                memcpy(socket_data, here_.fabric_data(), locality_defs::array_size);

            //                // Set src addr hints (FI_SOURCE must not be set in that case)
            //                free(hints->src_addr);
            //                hints->addr_format = na_ofi_prov_addr_format[na_ofi_domain->prov_type];
            //                hints->src_addr = src_addr;
            //                hints->src_addrlen = src_addrlen;
            //            }

            struct fid_ep* ep;
            int ret = fi_endpoint(domain, info, &ep, nullptr);
            if (ret)
                throw fabric_error(ret,
                    "fi_endpoint (too many threadlocal "
                    "endpoints?)");
/*
            if (hints)
            {
                // Prevent fi_freeinfo() from freeing src_add
                if (hints->src_addr) {
                    free(hints->src_addr);
                    hints->src_addr = NULL;
                }
                fi_freeinfo(hints);
                // free(socket_data);
            }
            if (new_hints)
            {
                fi_freeinfo(new_hints);
            }
*/
            return ep;
        }

        // --------------------------------------------------------------------
        struct fid_ep* new_endpoint_scalable(
            struct fid_domain* domain, struct fi_info* info, size_t threads, size_t &threads_allocated)
        {
            // don't allow multiple threads to call endpoint create at the same time
            scoped_lock lock(controller_mutex_);

            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);

            OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("fi_dupinfo")));
            struct fi_info* hints = fi_dupinfo(info);
            if (!hints)
                throw fabric_error(0, "fi_dupinfo");

            int flags = 0;
            struct fi_info* new_hints = nullptr;
            int ret = fi_getinfo(FI_VERSION(LIBFABRIC_FI_VERSION_MAJOR,
                                     LIBFABRIC_FI_VERSION_MINOR),
                nullptr, nullptr, flags, hints, &new_hints);
            if (ret)
                throw fabric_error(ret, "fi_getinfo");

            // Check the optimal number of TX and RX contexts supported by the provider
            size_t context_count = std::min(new_hints->domain_attr->tx_ctx_cnt, new_hints->domain_attr->rx_ctx_cnt);
            context_count = std::min(context_count, threads);
            OOMPH_DP_ONLY(cnt_deb, trace(debug::str<>("scalable endpoint")
                  , "Threads", debug::dec<3>(threads)
                  , "tx_ctx_cnt", debug::dec<3>(new_hints->domain_attr->tx_ctx_cnt)
                  , "rx_ctx_cnt", debug::dec<3>(new_hints->domain_attr->rx_ctx_cnt)
                  , "context_count", debug::dec<3>(context_count)
                  ));

//            if (context_count < threads || context_count <= 1)
//            {
//                OOMPH_DP_ONLY(cnt_err,
//                    error(debug::str<>("scalable endpoint unsupported")
//                          , "Threads", debug::dec<3>(threads)
//                          , "tx_ctx_cnt", debug::dec<3>(new_hints->domain_attr->tx_ctx_cnt)
//                          , "context_count", debug::dec<3>(context_count)
//                          ));
//                return nullptr;
//            }
            threads_allocated = context_count;
            new_hints->ep_attr->tx_ctx_cnt = context_count;
            new_hints->ep_attr->rx_ctx_cnt = context_count;

            struct fid_ep* ep;
            ret = fi_scalable_ep(domain, new_hints, &ep, nullptr);
            if (ret)
                throw fabric_error(ret, "fi_scalable_ep");

            fi_freeinfo(hints);
            return ep;
        }

        // --------------------------------------------------------------------
        endpoint_wrapper& get_rx_endpoint()
        {
            static auto rx = cnt_deb.make_timer(1, debug::str<>("get_rx_endpoint"));
            OOMPH_DP_ONLY(cnt_deb,timed(rx));

            if (endpoint_type_ == endpoint_type::scalable)
            {
                if (eps_->tl_srx_.get_ep() == nullptr)
                {
                    endpoint_wrapper ep;
                    bool ok = rx_endpoints_.pop(ep);
                    if (!ok) {
                        OOMPH_DP_ONLY(cnt_deb,
                                      error(debug::str<>("Scalable Ep"), "pop rx"
                                            , "ep", hpx::debug::ptr(ep.get_ep())
                                            , "tx cq", hpx::debug::ptr(ep.get_tx_cq())
                                            , "rx cq", hpx::debug::ptr(ep.get_rx_cq())));
                        throw std::runtime_error("rx endpoint wrapper pop fail");
                    }
                    eps_->tl_srx_ = stack_endpoint(
                                ep.get_ep(), ep.get_rx_cq(), ep.get_tx_cq(), ep.get_name(), &rx_endpoints_);
                    OOMPH_DP_ONLY(cnt_deb,
                                  trace(debug::str<>("Scalable Ep"), "pop rx"
                                        , "ep", hpx::debug::ptr(eps_->tl_srx_.get_ep())
                                        , "tx cq", hpx::debug::ptr(eps_->tl_srx_.get_tx_cq())
                                        , "rx cq", hpx::debug::ptr(eps_->tl_srx_.get_rx_cq())));
                }
                return eps_->tl_srx_.endpoint_;
            }
            // otherwise just return the normal Rx endpoint
            return eps_->ep_rx_;
        }

        // --------------------------------------------------------------------
        endpoint_wrapper& get_tx_endpoint()
        {
            static auto tx = cnt_deb.make_timer(1, debug::str<>("get_tx_endpoint"));
            OOMPH_DP_ONLY(cnt_deb, timed(tx));

            if (endpoint_type_ == endpoint_type::scalable)
            {
                if (eps_->tl_stx_.get_ep() == nullptr)
                {
                    endpoint_wrapper ep;
                    bool ok = tx_endpoints_.pop(ep);
                    if (!ok) {
                        OOMPH_DP_ONLY(cnt_deb,
                                      error(debug::str<>("Scalable Ep"), "pop tx"
                                            , "ep", hpx::debug::ptr(ep.get_ep())
                                            , "tx cq", hpx::debug::ptr(ep.get_tx_cq())
                                            , "rx cq", hpx::debug::ptr(ep.get_rx_cq())));
                        throw std::runtime_error("tx endpoint wrapper pop fail");
                    }
                    eps_->tl_stx_ = stack_endpoint(
                                ep.get_ep(), ep.get_rx_cq(), ep.get_tx_cq(), ep.get_name(), &tx_endpoints_);
                    OOMPH_DP_ONLY(cnt_deb,
                                  trace(debug::str<>("Scalable Ep"), "pop tx"
                                        , "ep", hpx::debug::ptr(eps_->tl_stx_.get_ep())
                                        , "tx cq", hpx::debug::ptr(eps_->tl_stx_.get_tx_cq())
                                        , "rx cq", hpx::debug::ptr(eps_->tl_stx_.get_rx_cq())));
                }
                return eps_->tl_stx_.endpoint_;
            }
            else if (endpoint_type_ == endpoint_type::threadlocal)
            {
                if (eps_->tl_tx_.get_ep() == nullptr)
                {
                    [[maybe_unused]] auto scp =
                        oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__, "threadlocal");
                    endpoint_wrapper ep;
                    bool ok = tx_endpoints_.pop(ep);
                    if (!ok) {
                        // create a completion queue for tx endpoint
                        fabric_info_->tx_attr->op_flags |= FI_COMPLETION;
                        auto tx_cq = create_completion_queue(
                            fabric_domain_, fabric_info_->tx_attr->size);

                        // setup an endpoint for sending messages
                        // note that the CQ needs FI_RECV even though its a Tx cq to keep
                        // some providers happy as they trigger an error if an endpoint
                        // has no Rx cq attached (progress bug)
                        auto ep_tx = new_endpoint_active(
                            fabric_domain_, fabric_info_, nullptr, false);
                        bind_queue_to_endpoint(ep_tx, tx_cq, FI_TRANSMIT | FI_RECV);
                        bind_address_vector_to_endpoint(ep_tx, av_);
                        enable_endpoint(ep_tx);
                        // set threadlocal endpoint wrapper
                        OOMPH_DP_ONLY(cnt_deb,
                            trace(debug::str<>("Threadlocal Ep"), "create Tx"
                                  , "ep", hpx::debug::ptr(ep_tx)
                                  , "tx cq", hpx::debug::ptr(tx_cq)
                                  , "rx cq", hpx::debug::ptr(nullptr)));
                        eps_->tl_tx_ = stack_endpoint(ep_tx, nullptr, tx_cq, "TX (TL) threadlocal", &tx_endpoints_);
                    }
                    else {
                        // set threadlocal endpoint wrapper
                        eps_->tl_tx_ = stack_endpoint(ep.get_ep(), nullptr, ep.get_tx_cq(), "TX (TL) threadlocal", &tx_endpoints_);
                        OOMPH_DP_ONLY(cnt_deb,
                            trace(debug::str<>("Threadlocal Ep"), "pop Tx"
                                  , "ep", hpx::debug::ptr(eps_->tl_tx_.get_ep())
                                  , "tx cq", hpx::debug::ptr(eps_->tl_tx_.get_tx_cq())
                                  , "rx cq", hpx::debug::ptr(eps_->tl_tx_.get_rx_cq())));
                    }
                }
                return eps_->tl_tx_.endpoint_;
            }
            else if (endpoint_type_ == endpoint_type::multiple)
            {
                [[maybe_unused]] auto scp =
                    oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__, "separate_endpoints_");
                return eps_->ep_tx_;
            }
            // shared tx/rx endpoint
            return eps_->ep_rx_;
        }

        // --------------------------------------------------------------------
        void bind_address_vector_to_endpoint(
            struct fid_ep* endpoint, struct fid_av* av)
        {
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);

            OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("Binding AV")));
            int ret = fi_ep_bind(endpoint, &av->fid, 0);
            if (ret)
                throw fabric_error(ret, "bind address_vector");
        }

        // --------------------------------------------------------------------
        void bind_queue_to_endpoint(
            struct fid_ep* endpoint, struct fid_cq*& cq, uint32_t cqtype)
        {
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);

            OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("Binding CQ")));
            int ret = fi_ep_bind(endpoint, &cq->fid, cqtype);
            if (ret)
                throw fabric_error(ret, "bind cq");
        }

        // --------------------------------------------------------------------
        fid_cq *bind_tx_queue_to_rx_endpoint(struct fid_ep* ep, bool needed)
        {
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);
            if (needed) {
                fabric_info_->tx_attr->op_flags |= FI_COMPLETION;
                fid_cq *tx_cq = create_completion_queue(
                    fabric_domain_, fabric_info_->tx_attr->size);
                // shared send/recv endpoint - bind send cq to the recv endpoint
                bind_queue_to_endpoint(ep, tx_cq, FI_TRANSMIT);
                return tx_cq;
            }
            return nullptr;
        }

        // --------------------------------------------------------------------
        void enable_endpoint(struct fid_ep* endpoint)
        {
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);

            OOMPH_DP_ONLY(cnt_deb,
                debug(debug::str<>("Enabling endpoint"), hpx::debug::ptr(endpoint)));
            int ret = fi_enable(endpoint);
            if (ret)
                throw fabric_error(ret, "fi_enable");
        }

        // --------------------------------------------------------------------
        locality get_endpoint_address(struct fid* id)
        {
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);

            locality::locality_data local_addr;
            std::size_t addrlen = locality_defs::array_size;
            OOMPH_DP_ONLY(cnt_deb,
                debug(
                    debug::str<>("Get address : size"), debug::dec<>(addrlen)));
            int ret = fi_getname(id, local_addr.data(), &addrlen);
            if (ret || (addrlen > locality_defs::array_size))
            {
                fabric_error(ret, "fi_getname - size error or other problem");
            }

            // optimized out when debug logging is false
            if (cnt_deb.is_enabled())
            {
                std::stringstream temp1;
                for (std::size_t i = 0; i < locality_defs::array_length; ++i)
                {
                    temp1 << debug::ipaddr(&local_addr[i]) << " - ";
                }
                OOMPH_DP_ONLY(cnt_deb,
                    debug(
                        debug::str<>("raw address data"), temp1.str().c_str()));
                std::stringstream temp2;
                for (std::size_t i = 0; i < locality_defs::array_length; ++i)
                {
                    temp2 << debug::hex<8>(local_addr[i]) << " - ";
                }
                OOMPH_DP_ONLY(cnt_deb,
                    debug(
                        debug::str<>("raw address data"), temp2.str().c_str()));
            }
            return locality(local_addr);
        }

        // --------------------------------------------------------------------
        fid_pep* create_passive_endpoint(
            struct fid_fabric* fabric, struct fi_info* info)
        {
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);

            struct fid_pep* ep;
            int ret = fi_passive_ep(fabric, info, &ep, nullptr);
            if (ret)
            {
                throw fabric_error(ret, "Failed to create fi_passive_ep");
            }
            return ep;
        }

        // --------------------------------------------------------------------
        // if we did not bootstrap, then fetch the list of all localities
        // from agas and insert each one into the address vector
        void exchange_addresses(fid_av* av, MPI_Comm comm, int rank, int size)
        {
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);

            OOMPH_DP_ONLY(cnt_deb,
                debug(
                    debug::str<>("initialize_localities"), size, "localities"));

            MPI_exchange_localities(av, comm, rank, size);
            debug_print_av_vector(size);
            OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("Done localities")));
        }

        // --------------------------------------------------------------------
        const locality& here() const
        {
            return here_;
        }

        // --------------------------------------------------------------------
        // returns true when all connections have been disconnected and none are active
        bool isTerminated()
        {
            return false;
            //return (qp_endpoint_map_.size() == 0);
        }

        // types we need for connection and disconnection callback functions
        // into the main parcelport code.
        typedef std::function<void(fid_ep* endpoint, uint32_t ipaddr)>
            ConnectionFunction;
        typedef std::function<void(fid_ep* endpoint, uint32_t ipaddr)>
            DisconnectionFunction;

        // --------------------------------------------------------------------
        void debug_print_av_vector(std::size_t N)
        {
            libfabric::locality addr;
            std::size_t addrlen = libfabric::locality_defs::array_size;
            for (std::size_t i = 0; i < N; ++i)
            {
                int ret = fi_av_lookup(
                    av_, fi_addr_t(i), addr.fabric_data_writable(), &addrlen);
                addr.set_fi_address(fi_addr_t(i));
                if ((ret == 0) &&
                    (addrlen == libfabric::locality_defs::array_size))
                {
                    OOMPH_DP_ONLY(cnt_deb,
                        debug(debug::str<>("address vector"), debug::dec<3>(i),
                            iplocality(addr)));
                }
                else
                {
                    throw std::runtime_error(
                        "debug_print_av_vector : address vector "
                        "traversal failure");
                }
            }
        }

        // --------------------------------------------------------------------
        progress_status poll_for_work_completions()
        {
            bool retry;
            int sends = 0;
            int recvs = 0;
            do
            {
                int tsend = poll_send_queue(get_tx_endpoint().get_tx_cq());
                int trecv = poll_recv_queue(get_rx_endpoint().get_rx_cq());
                sends += tsend;
                recvs += trecv;
                // we always retry until no new completion events are
                // found. this helps progress all messages
                retry = (tsend | trecv) != 0;
            } while (retry);
            return progress_status{sends, recvs, 0};
        }

        // --------------------------------------------------------------------
        int poll_send_queue(fid_cq* send_cq)
        {
            const int MAX_COMPLETIONS = 16;
            int ret;
            fi_cq_msg_entry entry[MAX_COMPLETIONS];
            // create a scoped block for the lock
            // when threadlocal endpoints are used, we do not need to lock
            bool threadlocal = (endpoint_type_ == endpoint_type::scalable ||
                                endpoint_type_ == endpoint_type::threadlocal);
            {
                auto lock = threadlocal ?
                            std::unique_lock<mutex_type>() :
                            std::unique_lock<mutex_type>(send_mutex_, std::try_to_lock_t{});

                // if we're not threadlocal and didn't get the lock,
                // then another thread is polling now, just exit
                if (!threadlocal && !lock.owns_lock())
                {
                    return 0;
                }

                static auto polling =
                    cnt_deb.make_timer(1, debug::str<>("poll send queue"));
                OOMPH_DP_ONLY(cnt_deb, timed(polling, hpx::debug::ptr(send_cq)));

                // poll for completions
                {
                    ret = fi_cq_read(send_cq, &entry[0], MAX_COMPLETIONS);
                }
                // if there is an error, retrieve it
                if (ret == -FI_EAVAIL)
                {
                    struct fi_cq_err_entry e = {};
                    int err_sz = fi_cq_readerr(send_cq, &e, 0);
                    (void) err_sz;

                    // flags might not be set correctly
                    if (e.flags == (FI_MSG | FI_SEND))
                    {
                        cnt_deb.error("txcq Error FI_EAVAIL for "
                                      "FI_SEND with len",
                            debug::hex<6>(e.len), "context",
                            hpx::debug::ptr(e.op_context));
                    }
                    if (e.flags & FI_RMA)
                    {
                        cnt_deb.error("txcq Error FI_EAVAIL for "
                                      "FI_RMA with len",
                            debug::hex<6>(e.len), "context",
                            hpx::debug::ptr(e.op_context));
                    }
                    operation_context* handler =
                        reinterpret_cast<operation_context*>(e.op_context);
                    handler->handle_error(e);
                    return 0;
                }
            }
            //
            // release the lock and process each completion
            //
            if (ret > 0)
            {
                int processed = 0;
                for (int i = 0; i < ret; ++i)
                {
                    ++sends_complete;
                    OOMPH_DP_ONLY(cnt_deb,
                        debug(debug::str<>("Completion"), debug::dec<2>(i)
                            , "txcq flags", fi_tostr(&entry[i].flags, FI_TYPE_OP_FLAGS), "(",
                            debug::dec<>(entry[i].flags), ")", "context",
                            hpx::debug::ptr(entry[i].op_context), "length",
                            debug::hex<6>(entry[i].len)));
                    if (entry[i].flags == (FI_TAGGED | FI_MSG | FI_SEND))
                    {
                        OOMPH_DP_ONLY(cnt_deb,
                            debug(debug::str<>("Completion"),
                                "txcq FI_MSG tagged send completion",
                                hpx::debug::ptr(entry[i].op_context)));

                        operation_context* handler = reinterpret_cast<operation_context*>(
                            entry[i].op_context);
                        processed += handler->handle_send_completion(threadlocal);

//                        throw fabric_error(ret, "FI_TAGGED | FI_MSG | FI_SEND");
                    }
                    else if (entry[i].flags == (FI_TAGGED | FI_SEND))
                    {
                        OOMPH_DP_ONLY(cnt_deb,
                            debug(debug::str<>("Completion"),
                                "txcq tagged send completion",
                                hpx::debug::ptr(entry[i].op_context)));

                        operation_context* handler = reinterpret_cast<operation_context*>(
                            entry[i].op_context);
                        processed += handler->handle_send_completion(threadlocal);
                                            }
                    else if (entry[i].flags == (FI_MSG | FI_SEND))
                    {
                        OOMPH_DP_ONLY(cnt_deb,
                            debug(debug::str<>("Completion"),
                                "txcq MSG send completion",
                                hpx::debug::ptr(entry[i].op_context)));

                        operation_context* handler = reinterpret_cast<operation_context*>(
                            entry[i].op_context);
                        processed += handler->handle_send_completion(threadlocal);

                        throw fabric_error(ret, "FI_MSG | FI_SEND");
                    }
                    else
                    {
                        cnt_deb.error("Received an unknown txcq completion",
                            debug::dec<>(entry[i].flags),
                            debug::bin<64>(entry[i].flags));
                        std::terminate();
                    }
                }
                return processed;
            }
            else if (ret == 0 || ret == -FI_EAGAIN)
            {
                // do nothing, we will try again on the next check
            }
            else
            {
                cnt_deb.error("unknown error in completion txcq read");
            }
            return 0;
        }

        // --------------------------------------------------------------------
        int poll_recv_queue(fid_cq* rx_cq)
        {
            const int MAX_COMPLETIONS = 16;
            int ret;
            fi_cq_msg_entry entry[MAX_COMPLETIONS];
            // create a scoped block for the lock
            // when scalable endpoints are used, we do not need to lock
            // threadlocal is not used for recv queues
            bool threadlocal = (endpoint_type_ == endpoint_type::scalable);
            {
                auto lock = threadlocal ?
                            std::unique_lock<mutex_type>() :
                            std::unique_lock<mutex_type>(send_mutex_, std::try_to_lock_t{});

                // if we're not threadlocal and didn't get the lock,
                // then another thread is polling now, just exit
                if (!threadlocal && !lock.owns_lock())
                {
                    return 0;
                }

                static auto polling =
                    cnt_deb.make_timer(1, debug::str<>("poll recv queue"));
                OOMPH_DP_ONLY(cnt_deb, timed(polling, hpx::debug::ptr(rx_cq)));

                // poll for completions
                {
                    ret = fi_cq_read(rx_cq, &entry[0], MAX_COMPLETIONS);
                }
                // if there is an error, retrieve it
                if (ret == -FI_EAVAIL)
                {
                    // read the full error status
                    struct fi_cq_err_entry e = {};
                    int err_sz = fi_cq_readerr(rx_cq, &e, 0);
                    (void) err_sz;
                    // from the manpage 'man 3 fi_cq_readerr'
                    if (e.err == FI_ECANCELED)
                    {
                        OOMPH_DP_ONLY(cnt_deb,
                            debug(debug::str<>("rxcq Cancelled"), "flags",
                                debug::hex<6>(e.flags), "len",
                                debug::hex<6>(e.len), "context",
                                hpx::debug::ptr(e.op_context)));
                        // the request was cancelled, we can simply exit
                        // as the canceller will have doone any cleanup needed
                        operation_context* handler =
                            reinterpret_cast<operation_context*>(e.op_context);
                        handler->handle_cancelled();
                        return 0;
                    }
                    else
                    {
                        cnt_deb.error("rxcq Error ??? ", "err",
                            debug::dec<>(-e.err), "flags",
                            debug::hex<6>(e.flags), "len", debug::hex<6>(e.len),
                            "context", hpx::debug::ptr(e.op_context), "error",
                            fi_cq_strerror(rx_cq, e.prov_errno, e.err_data,
                                (char*) e.buf, e.len));
                    }
                    operation_context* handler =
                        reinterpret_cast<operation_context*>(e.op_context);
                    if (handler)
                        handler->handle_error(e);
                    return 0;
                }
            }
            //
            // release the lock and process each completion
            //
            if (ret > 0)
            {
                int processed = 0;
                for (int i = 0; i < ret; ++i)
                {
                    ++recvs_complete;
                    OOMPH_DP_ONLY(cnt_deb,
                        debug(debug::str<>("Completion"), "rxcq flags",
                            fi_tostr(&entry[i].flags, FI_TYPE_OP_FLAGS), "(",
                            debug::dec<>(entry[i].flags), ")", "context",
                            hpx::debug::ptr(entry[i].op_context), "length",
                            debug::hex<6>(entry[i].len)));
                    if (entry[i].flags == (FI_TAGGED | FI_MSG | FI_RECV))
                    {
                        OOMPH_DP_ONLY(cnt_deb,
                            debug(debug::str<>("Completion"),
                                "rxcq FI_MSG tagged recv completion",
                                hpx::debug::ptr(entry[i].op_context)));

                        operation_context* handler = reinterpret_cast<operation_context*>(
                            entry[i].op_context);
                        processed += handler->handle_recv_completion(threadlocal);

//                        throw fabric_error(ret, "FI_TAGGED | FI_MSG | FI_RECV");
                    }
                    else if (entry[i].flags == (FI_TAGGED | FI_RECV))
                    {
                        OOMPH_DP_ONLY(cnt_deb,
                            debug(debug::str<>("Completion"),
                                "rxcq tagged recv completion",
                                hpx::debug::ptr(entry[i].op_context)));

                        operation_context* handler = reinterpret_cast<operation_context*>(
                            entry[i].op_context);
                        processed += handler->handle_recv_completion(threadlocal);
                    }
                    else if (entry[i].flags == (FI_MSG | FI_RECV))
                    {
                        OOMPH_DP_ONLY(cnt_deb,
                            debug(debug::str<>("Completion"),
                                "rxcq MSG recv completion",
                                hpx::debug::ptr(entry[i].op_context)));

                        operation_context* handler = reinterpret_cast<operation_context*>(
                            entry[i].op_context);
                        processed += handler->handle_recv_completion(threadlocal);

                        throw fabric_error(ret, "FI_MSG | FI_RECV");
                    }
                    else
                    {
                        cnt_deb.error("Received an unknown rxcq completion",
                            debug::dec<>(entry[i].flags),
                            debug::bin<64>(entry[i].flags));
                        std::terminate();
                    }
                }
                return processed;
            }
            else if (ret == 0 || ret == -FI_EAGAIN)
            {
                // do nothing, we will try again on the next check
            }
            else
            {
                cnt_deb.error("unknown error in completion rxcq read");
            }
            return 0;
        }

        // --------------------------------------------------------------------
        inline struct fid_domain* get_domain()
        {
            return fabric_domain_;
        }

        // --------------------------------------------------------------------
        struct fid_cq* create_completion_queue(
            struct fid_domain* domain, size_t size)
        {
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);

            struct fid_cq* cq;
            fi_cq_attr cq_attr = {};
            cq_attr.format = FI_CQ_FORMAT_MSG;
            cq_attr.wait_obj = FI_WAIT_NONE;
            cq_attr.wait_cond = FI_CQ_COND_NONE;
            cq_attr.size = size;
            cq_attr.flags = 0 /*FI_COMPLETION*/;
            OOMPH_DP_ONLY(
                cnt_deb, trace(debug::str<>("CQ size"), debug::dec<4>(size)));
            // open completion queue on fabric domain and set context to null
            int ret = fi_cq_open(domain, &cq_attr, &cq, nullptr);
            if (ret)
                throw fabric_error(ret, "fi_cq_open");
            return cq;
        }

        // --------------------------------------------------------------------
        fid_av* create_address_vector(struct fi_info* info, int N, int num_rx_contexts)
        {
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);

            fid_av* av;
            fi_av_attr av_attr = {fi_av_type(0),0,0,0,nullptr,nullptr,0};

            // number of addresses expected
            av_attr.count = N;

            // number of receive contexts used
            int rx_ctx_bits = 0;
            while (num_rx_contexts >> ++rx_ctx_bits);
            av_attr.rx_ctx_bits = rx_ctx_bits;

            // if contexts is nonzero, then we are using a single scalable endpoint
            av_attr.ep_per_node = (num_rx_contexts>0) ? 2 : 0;

            if (info->domain_attr->av_type != FI_AV_UNSPEC) {
                av_attr.type = info->domain_attr->av_type;
            }
            else {
                OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("map FI_AV_TABLE")));
                av_attr.type = FI_AV_TABLE;
            }

            OOMPH_DP_ONLY(cnt_deb, debug(debug::str<>("Creating AV")));
            int ret = fi_av_open(fabric_domain_, &av_attr, &av, nullptr);
            if (ret)
                throw fabric_error(ret, "fi_av_open");
            return av;
        }

        // --------------------------------------------------------------------
        libfabric::locality insert_address(fid_av* av, const libfabric::locality& address)
        {
            [[maybe_unused]] auto scp = oomph::cnt_deb.scope(hpx::debug::ptr(this), __func__);

            OOMPH_DP_ONLY(cnt_deb,
                trace(debug::str<>("inserting AV"), iplocality(address), hpx::debug::ptr(av)));
            fi_addr_t fi_addr = 0xffffffff;
            int ret = fi_av_insert(
                av, address.fabric_data(), 1, &fi_addr, 0, nullptr);
            if (ret < 0)
            {
                throw fabric_error(ret, "fi_av_insert");
            }
            else if (ret == 0)
            {
                cnt_deb.error("fi_av_insert called with existing address");
                fabric_error(ret, "fi_av_insert did not return 1");
            }
            // address was generated correctly, now update the locality with the fi_addr
            libfabric::locality new_locality(address, fi_addr);
            OOMPH_DP_ONLY(cnt_deb,
                trace(debug::str<>("AV add"), "rank", debug::dec<>(fi_addr),
                    iplocality(new_locality), "fi_addr",
                    debug::hex<4>(fi_addr)));
            return new_locality;
        }
    };

}}
