/*
 * ghex-org
 *
 * Copyright (c) 2014-2021, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "./context.hpp"
#include "./communicator.hpp"
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

const char *context_impl::get_transport_option(const std::string &opt) {
    if (opt == "name") {
        return "mpi";
    }
    else {
        return "unspecified";
    }
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

} // namespace oomph

#include "../src.cpp"
