/*
 * ghex-org
 *
 * Copyright (c) 2014-2021, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <oomph/context.hpp>
#include <oomph/barrier.hpp>
#include <gtest/gtest.h>
#include "./mpi_runner/mpi_test_fixture.hpp"
#include <iostream>
#include <iomanip>
#include <thread>
#include <numeric>

TEST_F(mpi_test_fixture, rank_barrier)
{
    using namespace oomph;
    auto ctxt = context(MPI_COMM_WORLD, false);
    auto comm = ctxt.get_communicator();
    auto comm2 = ctxt.get_communicator();

    barrier b(ctxt);
    for (int i = 0; i < 20; i++) { b.rank_barrier(); }
}

namespace oomph
{
class test_barrier
{
  public:
    barrier& br;

    void test_in_node1(context& ctxt)
    {
        std::vector<int> innode1_out(br.size());

        auto work = [&](int id)
        {
            auto comm = ctxt.get_communicator();
            auto comm2 = ctxt.get_communicator();
            innode1_out[id] = br.in_node1() ? 1 : 0;
        };
        std::vector<std::thread> ths;
        for (int i = 0; i < br.size(); ++i) { ths.push_back(std::thread{work, i}); }
        for (int i = 0; i < br.size(); ++i) { ths[i].join(); }
        EXPECT_EQ(std::accumulate(innode1_out.begin(), innode1_out.end(), 0), 1);
    }
};
} // namespace oomph

TEST_F(mpi_test_fixture, in_node1)
{
    using namespace oomph;
    auto        ctxt = context(MPI_COMM_WORLD, true);
    std::size_t n_threads = 4;
    barrier     b(ctxt, n_threads);

    oomph::test_barrier{b}.test_in_node1(ctxt);
}

TEST_F(mpi_test_fixture, in_barrier_1)
{
    using namespace oomph;
    auto        ctxt = context(MPI_COMM_WORLD, true);
    std::size_t n_threads = 4;
    barrier     b(ctxt, n_threads);

    auto comm = ctxt.get_communicator();
    auto comm2 = ctxt.get_communicator();

    for (int i = 0; i < 20; i++) { b.rank_barrier(); }
}

TEST_F(mpi_test_fixture, in_barrier)
{
    using namespace oomph;
    auto ctxt = context(MPI_COMM_WORLD, true);

    std::size_t n_threads = 4;
    barrier     b(ctxt, n_threads);

    auto work = [&]()
    {
        auto comm = ctxt.get_communicator();
        auto comm2 = ctxt.get_communicator();
        for (int i = 0; i < 10; i++)
        {
            comm.progress();
            b.thread_barrier();
        }
    };

    std::vector<std::thread> ths;
    for (size_t i = 0; i < n_threads; ++i) { ths.push_back(std::thread{work}); }
    for (size_t i = 0; i < n_threads; ++i) { ths[i].join(); }
}

TEST_F(mpi_test_fixture, full_barrier)
{
    using namespace oomph;
    auto ctxt = context(MPI_COMM_WORLD, true);

    std::size_t n_threads = 4;
    barrier     b(ctxt, n_threads);

    auto work = [&]()
    {
        auto comm = ctxt.get_communicator();
        auto comm3 = ctxt.get_communicator();
        for (int i = 0; i < 10; i++) { b(); }
    };

    std::vector<std::thread> ths;
    for (size_t i = 0; i < n_threads; ++i) { ths.push_back(std::thread{work}); }
    for (size_t i = 0; i < n_threads; ++i) { ths[i].join(); }
}

TEST_F(mpi_test_fixture, full_barrier_sendrecv)
{
    using namespace oomph;
    auto ctxt = context(MPI_COMM_WORLD, true);

    std::size_t n_threads = 4;
    barrier     b(ctxt, n_threads);

    auto work = [&](int tid)
    {
        auto comm = ctxt.get_communicator();
        auto comm2 = ctxt.get_communicator();
        int  s_rank = (tid < 3) ? comm.rank() : ((comm.rank() + 1) % comm.size());
        int  s_tag = comm.rank() * 10 + tid;
        int  r_rank = (tid > 0) ? comm.rank() : ((comm.rank() + comm.size() - 1) % comm.size());
        int  r_tag = (tid > 0) ? (comm.rank() * 10 + tid - 1) : (r_rank * 10 + n_threads - 1);

        auto s_buffer = comm.make_buffer<int>(1000);
        auto r_buffer = comm.make_buffer<int>(1000);
        for (auto& x : s_buffer) x = s_tag;
        auto r_req = comm.recv(r_buffer, r_rank, r_tag);
        auto s_req = comm.send(s_buffer, s_rank, s_tag);
        b();
        while (!(r_req.test() && s_req.test())) {};
        b();
    };

    std::vector<std::thread> ths;
    for (size_t i = 0; i < n_threads; ++i) { ths.push_back(std::thread{work, i}); }
    for (size_t i = 0; i < n_threads; ++i) { ths[i].join(); }
}
