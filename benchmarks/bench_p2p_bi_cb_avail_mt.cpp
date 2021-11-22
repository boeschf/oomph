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
    std::atomic<int> tail_send(0);
    std::atomic<int> tail_recv(0);
#else
    int sent(0);
    int received(0);
    int tail_send(0);
    int tail_recv(0);
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

        int       comm_cnt = 0, nlsend_cnt = 0, nlrecv_cnt = 0, submit_cnt = 0, submit_recv_cnt = 0;
        int       last_received = 0;
        int       last_sent = 0;
        int       dbg = 0, sdbg = 0, rdbg = 0;
        int       lsent = 0, lrecv = 0;
        const int delta_i = niter / 10;

        auto send_callback = [inflight, thread_id, &nlsend_cnt, &comm_cnt, &sent](
                                 message&, int, int tag) {
            int pthr = tag / inflight;
            if (pthr != thread_id) nlsend_cnt++;
            comm_cnt++;
            sent++;
        };

        auto recv_callback = [inflight, thread_id, &nlrecv_cnt, &comm_cnt, &received](
                                 message&, int, int tag) {
            int pthr = tag / inflight;
            if (pthr != thread_id) nlrecv_cnt++;
            //printf("rank %d thrid %d tag %d pthr %d\n", rank, thread_id, tag, pthr);
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

        // send/recv niter messages - as soon as a slot becomes free
        while (sent < niter || received < niter)
        {
            if (thread_id == 0 && dbg >= delta_i)
            {
                dbg = 0;
                std::cout << rank << " total bwdt MB/s:      "
                          << ((received - last_received + sent - last_sent) * size *
                                 (double)buff_size / 2) /
                                 t0.stoc()
                          << "\n";
                t0.tic();
                last_received = received;
                last_sent = sent;
            }

            if (rank == 0 && thread_id == 0 && rdbg >= delta_i)
            {
                std::cout << received << " received\n";
                rdbg = 0;
            }

            if (rank == 0 && thread_id == 0 && sdbg >= delta_i)
            {
                std::cout << sent << " sent\n";
                sdbg = 0;
            }

            for (int j = 0; j < inflight; j++)
            {
                if (rreqs[j].test())
                {
                    submit_recv_cnt += num_threads;
                    rdbg += num_threads;
                    dbg += num_threads;
                    rreqs[j] =
                        comm.recv(rmsgs[j], peer_rank, thread_id * inflight + j, recv_callback);
                    lrecv++;
                }
                //else
                //    comm.progress();

                // if(lsent < lrecv+2*inflight && sent < niter && smsgs[j].use_count() == 1)
                if (lsent < lrecv + 2 * inflight && sent < niter && (sreqs[j].test()))
                {
                    submit_cnt += num_threads;
                    sdbg += num_threads;
                    dbg += num_threads;
                    sreqs[j] =
                        comm.send(smsgs[j], peer_rank, thread_id * inflight + j, send_callback);
                    lsent++;
                }
                //else
                //    comm.progress();
            }
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
            std::cout << "rank " << rank << " thread " << thread_id << " sends submitted "
                      << submit_cnt / num_threads << " serviced " << comm_cnt
                      << ", non-local sends " << nlsend_cnt << " non-local recvs " << nlrecv_cnt
                      << std::endl;
        }

        // tail loops - submit RECV requests until
        // all SEND requests have been finalized.
        // This is because UCX cannot cancel SEND requests.
        // https://github.com/openucx/ucx/issues/1162
        {
            int incomplete_sends = 0;
            int send_complete = 0;

            // complete all posted sends
            do
            {
                comm.progress();
                // check if we have completed all our posted sends
                if (!send_complete)
                {
                    incomplete_sends = 0;
                    for (int j = 0; j < inflight; j++)
                    {
                        if (!sreqs[j].test()) incomplete_sends++;
                    }
                    if (incomplete_sends == 0)
                    {
                        // increase thread counter of threads that are done with the sends
                        tail_send++;
                        send_complete = 1;
                    }
                }
                // continue to re-schedule all recvs to allow the peer to complete
                for (int j = 0; j < inflight; j++)
                {
                    if (rreqs[j].test())
                    {
                        rreqs[j] =
                            comm.recv(rmsgs[j], peer_rank, thread_id * inflight + j, recv_callback);
                    }
                }
            } while (tail_send != num_threads);

            // We have all completed the sends, but the peer might not have yet.
            // Notify the peer and keep submitting recvs until we get his notification.
            send_request sf;
            recv_request rf;
            auto         smsg = comm.make_buffer<char>(1);
            auto         rmsg = comm.make_buffer<char>(1);

#ifdef OOMPH_BENCHMARKS_MT
#pragma omp master
#endif
            {
                sf = comm.send(
                    smsg, peer_rank, 0x80000); //, [](communicator_type::message_type, int, int){});
                rf = comm.recv(
                    rmsg, peer_rank, 0x80000); //, [](communicator_type::message_type, int, int){});
            }

            while (tail_recv == 0)
            {
                comm.progress();

                // schedule all recvs to allow the peer to complete
                for (int j = 0; j < inflight; j++)
                {
                    if (rreqs[j].test())
                    {
                        rreqs[j] =
                            comm.recv(rmsgs[j], peer_rank, thread_id * inflight + j, recv_callback);
                    }
                }

#ifdef OOMPH_BENCHMARKS_MT
#pragma omp master
#endif
                {
                    if (rf.test()) tail_recv = 1;
                }
            }
        }
        // peer has sent everything, so we can cancel all posted recv requests
        for (int j = 0; j < inflight; j++) { rreqs[j].cancel(); }
    }

    return 0;
}
