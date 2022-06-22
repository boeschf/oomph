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

#include <unistd.h> // sysconf
#include <stdlib.h> // posix_mem_align
#include <cstring>  // memset
#include <cstdlib>
#include <new>
#include <limits>

namespace oomph
{

template<class T>
struct aligned_allocator
{
    using value_type = T;

    aligned_allocator() = default;
    template<class U>
    constexpr aligned_allocator(const aligned_allocator<U>&) noexcept
    {
    }

    [[nodiscard]] T* allocate(std::size_t n)
    {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::bad_array_new_length();

        unsigned long align_size = sysconf(_SC_PAGESIZE);
        void*         b = nullptr;
        if (posix_memalign((void**)(&b), align_size, n * sizeof(T))) throw std::bad_alloc();

        std::memset(b, 0, n * sizeof(T));

        if (auto p = static_cast<T*>(b)) { return p; }

        throw std::bad_alloc();
    }

    void deallocate(T* p, std::size_t) noexcept { std::free(p); }
};

template<class T, class U>
bool
operator==(const aligned_allocator<T>&, const aligned_allocator<U>&)
{
    return true;
}
template<class T, class U>
bool
operator!=(const aligned_allocator<T>&, const aligned_allocator<U>&)
{
    return false;
}
} // namespace oomph
