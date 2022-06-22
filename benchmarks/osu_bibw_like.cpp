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

#include <cassert>
#include <vector>

#define OOMPH_BENCHMARKS_PURE_MPI
#define OOMPH_BENCHMARKS_PRINT_INTERMEDIATE

#include "./mpi_environment.hpp"
#include "./args.hpp"
#include "./timer.hpp"
#include "./utils.hpp"
#include "./aligned_allocator.hpp"

template<typename T>
using aligned_vector = std::vector<T, oomph::aligned_allocator<T>>;

template<typename B, typename R>
void
exchange(B& s_buf, B& r_buf, R& s_req, R& r_req, int rank, int s_tag, int r_tag)
{
    for (std::size_t j = 0; j < r_buf.size(); ++j)
        MPI_Irecv(r_buf[j].data(), r_buf[j].size(), MPI_CHAR, rank, r_tag, MPI_COMM_WORLD,
            &r_req[j]);

    for (std::size_t j = 0; j < s_buf.size(); ++j)
        MPI_Isend(s_buf[j].data(), s_buf[j].size(), MPI_CHAR, rank, s_tag, MPI_COMM_WORLD,
            &s_req[j]);

    MPI_Waitall(s_req.size(), s_req.data(), MPI_STATUS_IGNORE);
    MPI_Waitall(r_req.size(), r_req.data(), MPI_STATUS_IGNORE);
}

template<typename B, typename R>
void
exchange(int n, B& s_buf, B& r_buf, R& s_req, R& r_req, int rank, int s_tag, int r_tag)
{
    for (int i = 0; i < n; ++i) exchange(s_buf, r_buf, s_req, r_req, rank, s_tag, r_tag);
}

int
main(int argc, char* argv[])
{
    using namespace oomph;

    timer t, t0;
    t0.tic();

    args cmd_args(argc, argv);
    if (!cmd_args) return exit(argv[0]);
    bool const multi_threaded = (cmd_args.num_threads > 1);
    assert((!multi_threaded) && "multi threading not supported");
    mpi_environment env(multi_threaded, argc, argv);
    if (env.size != 2) return exit(argv[0]);

    auto const window_size = cmd_args.inflight;
    auto const size = cmd_args.buff_size;
    auto const iterations = (cmd_args.n_iter + window_size - 1) / window_size;
    auto const my_id = env.rank;
    int const  warmup = 5;
    int const  skip = 10 * (warmup + 1);
    int const  delta_i = iterations / 10;

    int send_tag = (my_id == 0 ? 100 : 10);
    int recv_tag = (my_id == 0 ? 10 : 100);
    int rank = (my_id == 0 ? 1 : 0);

    std::vector<MPI_Request> send_request(window_size);
    std::vector<MPI_Request> recv_request(window_size);

    using buffer_type = aligned_vector<unsigned char>;
    std::vector<buffer_type> s_buf(window_size);
    std::vector<buffer_type> r_buf(window_size);
    for (auto& v : s_buf) v.resize(size);
    for (auto& v : r_buf) v.resize(size);

    env.barrier();

    exchange(skip, s_buf, r_buf, send_request, recv_request, rank, send_tag, recv_tag);
    for (int i = 0; i < iterations; ++i)
    {
        exchange(warmup, s_buf, r_buf, send_request, recv_request, rank, send_tag, recv_tag);
        t.tic();
        exchange(s_buf, r_buf, send_request, recv_request, rank, send_tag, recv_tag);
#ifdef OOMPH_BENCHMARKS_PRINT_INTERMEDIATE
        double const t_ = t.toc();
        if (i % delta_i == 0)
            std::cout << my_id << " total bwdt  MB/s:      " << (2 * size / t_) * window_size
                      << "\n";
#else
        t.toc();
#endif
    }

    t0.toc();

    if (my_id == 0)
    {
        std::cout << "elapsed:   " << t0.sum() / 1.0e6 << "s" << std::endl;
        std::cout << "time:      " << t.sum() / 1.0e6 << "s" << std::endl;
        std::cout << "final MB/s:      " << 2 * ((iterations / t.sum()) * size) * window_size
                  << "\n";
    }
    return 0;
}
