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

#include <gtest/gtest.h>
#include "./mpi_runner/mpi_test_fixture.hpp"
#include <iostream>
#include <iomanip>
#include <thread>

TEST_F(mpi_test_fixture, channel_ctor)
{
    using namespace oomph;

    context ctxt(MPI_COMM_WORLD, false);

    //if (world_rank == 0)
    //{
    //    auto sender_f = ctxt.make_sender<int>(100, 1, 1);

    //    auto s = sender_f.get();
    //}

    //if (world_rank == 1)
    //{
    //    auto receiver_f = ctxt.make_receiver<int>(100, 0, 1);

    //    auto r = receiver_f.get();
    //}

    auto sender_f = ctxt.make_sender<int>(100, (world_rank + 1) % world_size, (world_rank + 1) % world_size);
    auto receiver_f = ctxt.make_receiver<int>(100, (world_rank + world_size - 1) % world_size, world_rank);

    while (true)
    {
        if (sender_f.is_ready() && receiver_f.is_ready()) break;
        if (!sender_f.is_ready()) sender_f.progress();
        if (!receiver_f.is_ready()) receiver_f.progress();
    }

    //auto s = sender_f.get();
    //auto r = receiver_f.get();

    //s.write([](channel::rma_buffer<int> b){ std::cout << "fill cb invoked" << std::endl; });
    //std::cout << s.scheduled_sends() << std::endl;

    //s.progress();
    //s.progress();
    //s.progress();
    //std::cout << s.scheduled_sends() << std::endl;

    //auto w = make_writer(s);
    //auto w = write([](channel::rma_buffer<int>* first, std::size_t count){}, s);
    //auto w1 = write([](channel::rma_buffer<int>* first, std::size_t count){}, s,s);
    //auto w2 = write([](channel::rma_buffer<int>* first, std::size_t count){}, &s,&s+1);

}
