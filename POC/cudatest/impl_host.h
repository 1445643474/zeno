#pragma once

#define FDB_IMPL_HOST 1
#define FDB_CONSTEXPR constexpr
#define FDB_HOST_DEVICE
#define FDB_DEVICE

#include <cstdlib>
#include <cstring>
#include <utility>
#include "vec.h"

namespace fdb {

static void synchronize() {
}

struct ParallelConfig {
    size_t block_size{1024};
    size_t saturation{1};
};

template <class Kernel>
static void parallel_for(size_t dim, Kernel kernel, ParallelConfig cfg = {1024, 1}) {
    #pragma omp parallel for
    for (size_t i = 0; i < dim; i++) {
        kernel(std::as_const(i));
    }
}

template <class Kernel>
static void parallel_for(vec2S dim, Kernel kernel, ParallelConfig cfg = {32, 1}) {
    parallel_for(dim[0] * dim[1], [=] (size_t i) {
        size_t y = i / dim[0];
        size_t x = i % dim[0];
        vec2S idx(x, y);
        kernel(std::as_const(idx));
    });
}

template <class Kernel>
static void parallel_for(vec3S dim, Kernel kernel, ParallelConfig cfg = {8, 1}) {
    parallel_for(dim[0] * dim[1] * dim[2], [=] (size_t i) {
        size_t z = i / dim[1];
        size_t j = i % dim[1];
        size_t y = j / dim[0];
        size_t x = j % dim[0];
        vec3S idx(x, y, z);
        kernel(std::as_const(idx));
    });
}

static void memcpy_d2d(void *dst, const void *src, size_t n) {
    std::memcpy(dst, src, n);
}

static void memcpy_d2h(void *dst, const void *src, size_t n) {
    std::memcpy(dst, src, n);
}

static void memcpy_h2d(void *dst, const void *src, size_t n) {
    std::memcpy(dst, src, n);
}

static void *allocate(size_t n) {
    return std::malloc(n);
}

static void deallocate(void *p) {
    std::free(p);
}

static void *reallocate(void *p, size_t old_n, size_t new_n) {
    return std::realloc(p, new_n);
}

}
