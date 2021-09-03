/*
 * GridTools
 *
 * Copyright (c) 2014-2021, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <oomph/context.hpp>
#include <gtest/gtest.h>
#include "./mpi_runner/mpi_test_fixture.hpp"
#include <iostream>
#include <iomanip>
#include <thread>

const std::size_t size = 1024;
const int         num_threads = 4;

TEST_F(mpi_test_fixture, context_ordered)
{
    using namespace oomph;
    using rank_type = communicator::rank_type;
    using tag_type = communicator::tag_type;
    auto ctxt = context(MPI_COMM_WORLD, true);

    auto func = [&ctxt](int tid) {
        auto comm = ctxt.get_communicator();
        auto smsg_1 = comm.make_buffer<int>(size);
        auto smsg_2 = comm.make_buffer<int>(size);
        auto rmsg_1 = comm.make_buffer<int>(size);
        auto rmsg_2 = comm.make_buffer<int>(size);
        bool      sent_1 = false;
        bool      sent_2 = false;
        if (comm.rank() == 0)
        {
            const int payload_offset = 1 + tid;
            for (unsigned int i = 0; i < size; ++i)
            {
                smsg_1[i] = i + payload_offset;
                smsg_2[i] = i + payload_offset + 1;
            }
            std::vector<rank_type> neighs(comm.size()>1 ? comm.size() - 1 : 1, 0);
            for (int i = 1; i < comm.size(); ++i) neighs[i - 1] = i;

            comm.send_multi(std::move(smsg_1), neighs, tid,
                [&sent_1](decltype(smsg_1), std::vector<rank_type>, tag_type) { sent_1 = true; });

            comm.send_multi(std::move(smsg_2), neighs, tid,
                [&sent_2](decltype(smsg_2), std::vector<rank_type>, tag_type) { sent_2 = true; });

        }
        if (comm.rank() > 0 || comm.size() == 1)
        {
            // ordered sends/recvs with same tag should arrive in order
            comm.recv(rmsg_1, 0, tid).wait();
            comm.recv(rmsg_2, 0, tid).wait();

            // check message
            const int payload_offset = 1 + tid;
            for (unsigned int i = 0; i < size; ++i)
            {
                EXPECT_EQ(rmsg_1[i], i + payload_offset);
                EXPECT_EQ(rmsg_2[i], i + payload_offset + 1);
            }
        }
        if (comm.rank() == 0)
            while (!sent_1 || !sent_2) { comm.progress(); }
    };

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) threads.push_back(std::thread{func, i});
    for (auto& t : threads) t.join();
}

TEST_F(mpi_test_fixture, context_multi)
{
    using namespace oomph;
    using rank_type = communicator::rank_type;
    using tag_type = communicator::tag_type;
    auto ctxt_1 = context(MPI_COMM_WORLD, true);
    auto ctxt_2 = context(MPI_COMM_WORLD, true);

    auto func = [&ctxt_1, &ctxt_2](int tid_1, int tid_2) {
        auto         comm_1 = ctxt_1.get_communicator();
        auto         msg_1 = comm_1.make_buffer<int>(size);
        auto         comm_2 = ctxt_2.get_communicator();
        auto         msg_2 = comm_2.make_buffer<int>(size);
        bool         sent_1 = false;
        bool         sent_2 = false;
        recv_request req_1;
        recv_request req_2;

        if (comm_1.rank() == 0)
        {
            if (comm_2.rank() != 0) { EXPECT_TRUE(false); }
        }

        if (comm_1.rank() == 0)
        {
            const int payload_offset = 1 + tid_1;
            for (unsigned int i = 0; i < size; ++i) msg_1[i] = i + payload_offset;
            std::vector<rank_type> neighs(comm_1.size() - 1);
            for (int i = 1; i < comm_1.size(); ++i) neighs[i - 1] = i;
            comm_1.send_multi(std::move(msg_1), neighs, tid_1,
                [&sent_1](decltype(msg_1), std::vector<rank_type>, tag_type) { sent_1 = true; });
        }
        else
        {
            req_1 = comm_1.recv(msg_1, 0, tid_1);
        }

        if (comm_2.rank() == 0)
        {
            const int payload_offset = 1 + tid_2;
            for (unsigned int i = 0; i < size; ++i) msg_2[i] = i + payload_offset;
            std::vector<rank_type> neighs(comm_2.size() - 1);
            for (int i = 1; i < comm_2.size(); ++i) neighs[i - 1] = i;
            comm_2.send_multi(std::move(msg_2), neighs, tid_2,
                [&sent_2](decltype(msg_2), std::vector<rank_type>, tag_type) { sent_2 = true; });
        }
        else
        {
            req_2 = comm_2.recv(msg_2, 0, tid_2);
        }

        if (comm_2.rank() == 0)
            while (!sent_2) { comm_2.progress(); }

        if (comm_1.rank() == 0)
            while (!sent_1) { comm_1.progress(); }

        if (comm_2.rank() != 0)
        {
            req_2.wait();
            // check message
            const int payload_offset = 1 + tid_2;
            for (unsigned int i = 0; i < size; ++i) EXPECT_EQ(msg_2[i], i + payload_offset);
        }

        if (comm_1.rank() != 0)
        {
            req_1.wait();
            // check message
            const int payload_offset = 1 + tid_1;
            for (unsigned int i = 0; i < size; ++i) EXPECT_EQ(msg_1[i], i + payload_offset);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) threads.push_back(std::thread{func, i, i + 100});
    for (auto& t : threads) t.join();
}
