/*
 * GridTools
 *
 * Copyright (c) 2014-2021, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "./controller.hpp"
#include "./communicator.hpp"
#include "./context.hpp"

//#include "./send_channel.hpp"
//#include "./recv_channel.hpp"

namespace oomph
{
communicator_impl*
context_impl::get_communicator()
{
    auto comm = new communicator_impl{this};
    m_comms_set.insert(comm);
    return comm;
}

//send_channel_base::send_channel_base(communicator& comm, std::size_t size, std::size_t T_size,
//    communicator::rank_type dst, communicator::tag_type tag, std::size_t levels)
//: m_impl(comm.m_impl, size, T_size, dst, tag, levels)
//{
//}
//
//recv_channel_base::recv_channel_base(communicator& comm, std::size_t size, std::size_t T_size,
//    communicator::rank_type src, communicator::tag_type tag, std::size_t levels)
//: m_impl(comm.m_impl, size, T_size, src, tag, levels)
//{
//}

// cppcheck-suppress ConfigurationNotChecked
static hpx::debug::enable_print<false> src_deb("__SRC__");

std::shared_ptr<controller_type> context_impl::init_libfabric_controller(oomph::context_impl *ctx, MPI_Comm comm, int rank, int size, int threads)
{
    // static std::atomic_flag initialized = ATOMIC_FLAG_INIT;
    // if (initialized.test_and_set()) return;

    // only allow one thread to pass, make other wait
    static std::mutex m_init_mutex;
    std::lock_guard<std::mutex> lock(m_init_mutex);
    static std::shared_ptr<controller_type> instance(nullptr);
    if (!instance.get()) {
        OOMPH_DP_ONLY(src_deb, debug(hpx::debug::str<>("New Controller")));
        instance.reset(new controller_type(
                OOMPH_LIBFABRIC_PROVIDER,
                OOMPH_LIBFABRIC_DOMAIN,
                comm, rank, size, threads
            ));
    }
    return instance;
}

} // namespace oomph

#include "../src.cpp"
