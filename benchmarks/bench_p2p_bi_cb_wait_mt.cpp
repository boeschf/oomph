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
#include <oomph/barrier.hpp>
#include "./mpi_environment.hpp"
#include "./args.hpp"
#include "./timer.hpp"
#include "./utils.hpp"
#include <vector>

int
main(int argc, char** argv)
{
    using namespace oomph;
    using message = oomph::message_buffer<char>;

    args cmd_args(argc, argv);
    if (!cmd_args) return exit(argv[0]);
    bool const multi_threaded = (cmd_args.num_threads > 1);

    mpi_environment env(multi_threaded, argc, argv);
    if (env.size != 2) return exit(argv[0]);

    context ctxt(MPI_COMM_WORLD, multi_threaded);
    barrier b(ctxt, cmd_args.num_threads);
    timer   t0;
    timer   t1;

    const auto inflight = cmd_args.inflight;
    const auto num_threads = cmd_args.num_threads;
    const auto buff_size = cmd_args.buff_size;
    const auto niter = cmd_args.n_iter;

    if (env.rank == 0)
    {
        std::cout << "inflight = " << inflight << std::endl;
        std::cout << "size     = " << buff_size << std::endl;
        std::cout << "N        = " << niter << std::endl;
    }

#ifdef OOMPH_BENCHMARKS_MT
    std::atomic<int> sent(0);
    std::atomic<int> received(0);
#else
    int sent(0);
    int received(0);
#endif

#ifdef OOMPH_BENCHMARKS_MT
#pragma omp parallel
#endif
    {
        auto       comm = ctxt.get_communicator();
        const auto rank = comm.rank();
        const auto size = comm.size();
        const auto thread_id = THREADID;
        const auto peer_rank = (rank + 1) % size;

        int       comm_cnt = 0, nlsend_cnt = 0, nlrecv_cnt = 0;
        int       i = 0, dbg = 0;
        int       last_i = 0;
        const int delta_i = niter / 10;

        auto send_callback = [inflight, thread_id, &nlsend_cnt, &comm_cnt, &sent](
                                 message&, int, int tag) {
            // std::cout << "send callback called " << rank << " thread "
            // << omp_get_thread_num() << " tag " << tag << "\n";
            int pthr = tag / inflight;
            if (pthr != thread_id) nlsend_cnt++;
            comm_cnt++;
            sent++;
        };

        auto recv_callback = [inflight, thread_id, &nlrecv_cnt, &comm_cnt, &received](
                                 message&, int, int tag) {
            // std::cout << "recv callback called " << rank << " thread "
            // << omp_get_thread_num() << " tag " << tag << "\n";
            int pthr = tag / inflight;
            if (pthr != thread_id) nlrecv_cnt++;
            comm_cnt++;
            received++;
        };

        if (thread_id == 0 && rank == 0)
        { std::cout << "\n\nrunning test " << __FILE__ << "\n\n"; };

        std::vector<message>      smsgs(inflight);
        std::vector<message>      rmsgs(inflight);
        std::vector<send_request> sreqs(inflight);
        std::vector<recv_request> rreqs(inflight);
        for (int j = 0; j < inflight; j++)
        {
            smsgs[j] = comm.make_buffer<char>(buff_size);
            rmsgs[j] = comm.make_buffer<char>(buff_size);
            for (auto& c : smsgs[j]) c = 0;
            for (auto& c : rmsgs[j]) c = 0;
        }

        b();

        if (thread_id == 0)
        {
            if (rank == 0) std::cout << "number of threads: " << num_threads << "\n";
            t0.tic();
            t1.tic();
        }

        // send / recv niter messages, work in inflight requests at a time
        while (i < niter)
        {
            // ghex barrier not needed here (all comm finished), and VERY SLOW
            // barrier.in_node(comm);
#ifdef OOMPH_BENCHMARKS_MT
#pragma omp barrier
#endif

            if (thread_id == 0 && dbg >= delta_i)
            {
                dbg = 0;
                std::cout << rank << " total bwdt MB/s:      "
                          << ((i - last_i) * size * (double)buff_size) / t0.stoc() << "\n";
                t0.tic();
                last_i = i;
            }

            // submit inflight requests
            for (int j = 0; j < inflight; j++)
            {
                dbg += num_threads;
                i += num_threads;

                rreqs[j] = comm.recv(rmsgs[j], peer_rank, thread_id * inflight + j, recv_callback);
                sreqs[j] = comm.send(smsgs[j], peer_rank, thread_id * inflight + j, send_callback);
            }

            // complete all inflight requests before moving on
            //while (sent < num_threads * inflight || received < num_threads * inflight)
            //{ comm.progress(); }
            comm.wait_all();

            // ghex barrier not needed here (all comm finished), and VERY SLOW
            // barrier.in_node(comm);
#ifdef OOMPH_BENCHMARKS_MT
#pragma omp barrier
#endif
            sent = 0;
            received = 0;
        }

        b();

        if (thread_id == 0 && rank == 0)
        {
            const auto t = t1.toc();
            std::cout << "time:                   " << t / 1000000 << "s\n";
            std::cout << "final MB/s:             " << (niter * size * (double)buff_size) / t
                      << std::endl;
        }

        b();

#ifdef OOMPH_BENCHMARKS_MT
#pragma omp critical
#endif
        {
            std::cout << "rank " << rank << " thread " << thread_id << " serviced " << comm_cnt
                      << ", non-local sends " << nlsend_cnt << " non-local recvs " << nlrecv_cnt
                      << std::endl;
        }

        // tail loops - not needed in wait benchmarks
    }

    return 0;
}
