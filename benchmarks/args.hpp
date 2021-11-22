/*
 * GridTools
 *
 * Copyright (c) 2014-2021, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <cstdlib>
#include <cstring>
#include <iostream>

#ifndef OOMPH_BENCHMARKS_PURE_MPI
#include <oomph/utils.hpp>
#endif

#ifdef OOMPH_BENCHMARKS_MT
#include <omp.h>
#endif

namespace oomph
{
struct args
{
    bool is_valid = true;
    int  n_iter = 0;
    int  buff_size = 0;
    int  inflight = 0;
    int  num_threads = 1;

    args(int argc, char** argv)
    {
        if (argc != 4)
        {
            is_valid = false;
#ifndef OOMPH_BENCHMARKS_PURE_MPI
            if (argc == 2 && !std::strcmp(argv[1], "-c")) print_config();
#endif
        }
        else
        {
            n_iter = std::atoi(argv[1]);
            buff_size = std::atoi(argv[2]);
            inflight = std::atoi(argv[3]);

#ifdef OOMPH_BENCHMARKS_MT
#pragma omp parallel
            {
#pragma omp master
                num_threads = omp_get_num_threads();
            }
#endif
        }
    }

    operator bool() const noexcept { return is_valid; }
};

} // namespace oomph
