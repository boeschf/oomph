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
#include <vector>
#include <thread>

void
test_1(oomph::communicator& comm, unsigned int size, int thread_id = 0)
{
    EXPECT_TRUE(comm.size() > 0);
    auto msg = comm.make_buffer<int>(size);

    if (comm.rank() == 0)
    {
        std::vector<int> dsts(comm.size()>1 ? comm.size()-1 : 1, 0);
        for (unsigned int i = 0; i < size; ++i) msg[i] = i;
        for (int d = 1; d<comm.size(); ++d) dsts[d-1] = d;

        EXPECT_EQ(comm.scheduled_sends(), 0);
        EXPECT_EQ(comm.scheduled_recvs(), 0);

        comm.send_multi(msg, dsts, 42 + 42 + thread_id).wait();

        EXPECT_EQ(comm.scheduled_sends(), 0);
        EXPECT_EQ(comm.scheduled_recvs(), 0);
    }
    if (comm.rank() > 0 || comm.size() == 1)
    {
        EXPECT_EQ(comm.scheduled_sends(), 0);
        EXPECT_EQ(comm.scheduled_recvs(), 0);

        auto req = comm.recv(msg, 0, 42);

        EXPECT_EQ(comm.scheduled_sends(), 0);
        EXPECT_EQ(comm.scheduled_recvs(), 1);

        EXPECT_TRUE(req.cancel());

        EXPECT_EQ(comm.scheduled_sends(), 0);
        EXPECT_EQ(comm.scheduled_recvs(), 0);

        comm.recv(msg, 0, 42 + 42 + thread_id).wait();

        EXPECT_EQ(comm.scheduled_sends(), 0);
        EXPECT_EQ(comm.scheduled_recvs(), 0);

        for (unsigned int i = 0; i < size; ++i) EXPECT_EQ(msg[i], i);
    }
}

TEST_F(mpi_test_fixture, test_cancel_request)
{
    using namespace oomph;
    auto ctxt = context(MPI_COMM_WORLD, false);
    auto comm = ctxt.get_communicator();
    test_1(comm, 1);
    test_1(comm, 32);
    test_1(comm, 4096);
}

TEST_F(mpi_test_fixture, test_cancel_request_mt)
{
    using namespace oomph;
    auto        ctxt = context(MPI_COMM_WORLD, true);
    std::size_t n_threads = 4;

    std::vector<std::thread> threads;
    threads.reserve(n_threads);
    for (size_t i = 0; i < n_threads; ++i)
        threads.push_back(std::thread{[&ctxt, i]() {
            auto comm = ctxt.get_communicator();
            test_1(comm, 1, i);
            test_1(comm, 32, i);
            test_1(comm, 4096, i);
        }});
    for (auto& t : threads) t.join();
}

void
test_2(oomph::communicator& comm, unsigned int size, int thread_id = 0)
{
    EXPECT_TRUE(comm.size() > 0);
    auto msg = comm.make_buffer<int>(size);
    using msg_t = decltype(msg);

    if (comm.rank() == 0)
    {
        std::vector<int> dsts(comm.size()>1 ? comm.size()-1 : 1, 0);
        for (unsigned int i = 0; i < size; ++i) msg[i] = i;
        for (int d = 1; d<comm.size(); ++d) dsts[d-1] = d;

        EXPECT_EQ(comm.scheduled_sends(), 0);
        EXPECT_EQ(comm.scheduled_recvs(), 0);

        comm.send_multi(msg, dsts, 42 + 42 + thread_id).wait();

        EXPECT_EQ(comm.scheduled_sends(), 0);
        EXPECT_EQ(comm.scheduled_recvs(), 0);
    }
    if (comm.rank() > 0 || comm.size() == 1)
    {
        EXPECT_EQ(comm.scheduled_sends(), 0);
        EXPECT_EQ(comm.scheduled_recvs(), 0);

        int  counter = 0;
        auto h = comm.recv(msg, 0, 42, [&counter](msg_t&, int, int) { ++counter; });

        EXPECT_EQ(comm.scheduled_sends(), 0);
        EXPECT_EQ(comm.scheduled_recvs(), 1);

        comm.progress();
        comm.progress();
        comm.progress();
        comm.progress();

        EXPECT_EQ(comm.scheduled_sends(), 0);
        EXPECT_EQ(comm.scheduled_recvs(), 1);
        EXPECT_EQ(counter, 0);

        EXPECT_TRUE(h.cancel());
        comm.recv(msg, 0, 42 + 42 + thread_id).wait();

        EXPECT_EQ(comm.scheduled_sends(), 0);
        EXPECT_EQ(comm.scheduled_recvs(), 0);

        for (unsigned int i = 0; i < size; ++i) EXPECT_EQ(msg[i], i);
    }
}

TEST_F(mpi_test_fixture, test_cancel_cb)
{
    using namespace oomph;
    auto ctxt = context(MPI_COMM_WORLD, false);
    auto comm = ctxt.get_communicator();
    test_2(comm, 1);
    test_2(comm, 32);
    test_2(comm, 4096);
}

TEST_F(mpi_test_fixture, test_cancel_cb_mt)
{
    using namespace oomph;
    auto        ctxt = context(MPI_COMM_WORLD, true);
    std::size_t n_threads = 4;

    std::vector<std::thread> threads;
    threads.reserve(n_threads);
    for (size_t i = 0; i < n_threads; ++i)
        threads.push_back(std::thread{[&ctxt, i]() {
            auto comm = ctxt.get_communicator();
            test_2(comm, 1, i);
            test_2(comm, 32, i);
            test_2(comm, 4096, i);
        }});
    for (auto& t : threads) t.join();
}
