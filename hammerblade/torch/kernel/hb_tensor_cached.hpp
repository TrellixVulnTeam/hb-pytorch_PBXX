//====================================================================
// Cached Tensor
// 03/09/2020 Bandhav Veluri
//====================================================================

#ifndef _HB_TENSOR_CACHED_HPP
#define _HB_TENSOR_CACHED_HPP

#include <cstdint>
#include <climits>
#include <bsg_manycore.h>
#include <hb_assert.hpp>
#include <hb_common.hpp>
#include <hb_tensor.hpp>

template <typename DT, int32_t dims=-1, uint32_t cache_size = 8>
class HBTensorCached : public HBTensorImpl<__remote DT, uint32_t> {
  private:
    uint32_t strides[dims];
    uint32_t sizes[dims];

    static constexpr uint32_t cache_numel = cache_size /
                                            (sizeof(uint32_t) + sizeof(float));
    uint32_t cache_tag[cache_numel];
    float cache_data[cache_numel] = {0};

    uint32_t hits = 0;
    uint32_t misses = 0;

  public:
    HBTensorCached(hb_tensor_t* t) :
      HBTensorImpl<__remote DT, uint32_t>(
        t->N,
        (uint32_t) dims,
        strides,
        sizes,
        (__remote DT*) ((intptr_t) t->data)
      ) {
        hb_assert_msg(
          t->dims == dims,
          "error: HBTensor dims don't match offloaed tensor dims");

        uint32_t* strides_remote = (uint32_t*) ((intptr_t) t->strides);
        uint32_t* sizes_remote = (uint32_t*) ((intptr_t) t->sizes);

        // Move strides and sizes to scratchpad
        for(int i=0; i<dims; ++i) {
          strides[i] = strides_remote[i];
          sizes[i] = sizes_remote[i];
        }

        // Invalidate cache
        UNROLL(32) for(int i = 0; i < cache_numel; ++i) {
          cache_tag[i] = UINT_MAX;
        }
      }

    __attribute__((noinline))
    void prefetch_impl(uint32_t* NOALIAS tag_ptr,
                       DT* NOALIAS data_ptr,
                       __remote DT* remote_data_ptr,
                       uint32_t start, uint32_t end) {
      UNROLL(16) for(uint32_t i = start; i < end; ++i) {
        data_ptr[i % cache_numel] = remote_data_ptr[i];
        tag_ptr[i % cache_numel] = i;
      }
    }

    void prefetch(uint32_t off) {
      uint32_t end = std::min(off + cache_numel, this->N);

      prefetch_impl(cache_tag, cache_data,
                    (__remote DT*) this->data,
                    off, end);
    }

    template<typename ...T>
    DT cached_read(T... indices) {
      uint32_t off = this->offset(indices...);
      uint32_t ci = off % cache_numel;

      if(cache_tag[ci] == off) {
        hits++;
        return cache_data[ci];
      } else {
        misses++;
      }

      prefetch(off);
      DT rdata = this->data[off];
      cache_tag[ci] = off;
      cache_data[ci] = rdata;
      return rdata;
    }

    void print_stats() {
      bsg_printf("hits: %d misses: %d\n", hits, misses);
    }
};

#endif // _HB_TENSOR_CACHED_HPP
