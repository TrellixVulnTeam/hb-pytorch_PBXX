//====================================================================
// Element-wise for helper function
// 03/12/2020 Lin Cheng (lc873@cornell.edu)
//
// Note: assuming a 3D tensor, and you access it with (x, y, z)
// Plain tensor has indices numbered as (0, 1, 2)
// BUT iterator tensor has indices numbered as (2, 1, 0)
//====================================================================

#ifndef _HB_TILED_FOR_HPP
#define _HB_TILED_FOR_HPP

#include <map>
#include <math.h>
#include <initializer_list>
#include <hb_assert.hpp>
#include <hb_tensor.hpp>

// =========================================================
// Linear index to offset
// =========================================================
template<typename scalar_t>
inline uint32_t offset_calc(uint32_t idx, HBTensor<scalar_t> tensor) {
  uint32_t* strides = tensor.get_strides();
  uint32_t* sizes = tensor.get_sizes();
  uint32_t offset = 0;
  for(uint32_t i = 0; i < tensor.ndim(); i++) {
    uint32_t dimx = idx % sizes[i];
    idx /= sizes[i];
    offset += dimx * strides[i];
  }
  return offset;
}

// =========================================================
// Tiled range calculation
// hb_range -> [start, end)
// =========================================================
typedef struct hb_range {
  size_t start;
  size_t end;
} hb_range;

inline void calc_range(hb_range* range, size_t numel) {
  // per pod chunk
  size_t len_per_pod  = numel / BSG_POD_DIM + 1;
  // chunk range
  size_t pod_start    = len_per_pod * __bsg_pod_id;
  size_t pod_end      = pod_start + len_per_pod;
  pod_end = (pod_end > numel) ? numel : pod_end;
  if (pod_start >= pod_end) {
    range->start = 0;
    range->end   = 0;
    return;
  }
  size_t pod_size     = pod_end - pod_start;

  // per tile range within a pod
  size_t len_per_tile = pod_size / (bsg_tiles_X * bsg_tiles_Y) + 1;
  size_t start        = len_per_tile * __bsg_id;
  size_t end          = start + len_per_tile;
  end = (end > pod_size) ? pod_size : end;
  if (start >= end) {
    range->start = 0;
    range->end   = 0;
    return;
  }

  // range in global idx
  range->start = pod_start + start;
  range->end   = pod_start + end;

  return;
}

// =========================================================
// Pointwise for -- Ternary
// =========================================================

template<typename scalar_t, typename F>
inline void hb_foreach(HBTensor<scalar_t> res,
                       HBTensor<scalar_t> input,
                       HBTensor<scalar_t> tensor1,
                       HBTensor<scalar_t> tensor2,
                       F functor) {
  char* data[4];
  data[0] = res.data_ptr();
  data[1] = input.data_ptr();
  data[2] = tensor1.data_ptr();
  data[3] = tensor2.data_ptr();

  // is_trivial_1d
  if(res.ndim() == 1) {

    //-----------------------------
    // collect metadata
    //-----------------------------
    uint32_t strides[4];
    strides[0] = (res.get_strides())[0];
    strides[1] = (input.get_strides())[0];
    strides[2] = (tensor1.get_strides())[0];
    strides[3] = (tensor2.get_strides())[0];

    //------------------------------
    // in the case where stride is 0
    //------------------------------
    scalar_t fixed_data[4];
    for (size_t i = 0; i < 4; i++) {
      if (strides[i] == 0) {
        fixed_data[i] = *(scalar_t*)data[i];
        data[i] = (char*)&fixed_data[i];
      }
    }

    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    size_t start = 0;
    size_t end = res.numel();
    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0]);
      scalar_t* input_dp = (scalar_t*)(data[1]);
      scalar_t* tensor1_dp = (scalar_t*)(data[2]);
      scalar_t* tensor2_dp = (scalar_t*)(data[3]);
      *res_dp = functor(*input_dp, *tensor1_dp, *tensor2_dp);
      data[0] += strides[0];
      data[1] += strides[1];
      data[2] += strides[2];
      data[3] += strides[3];
    }
  } else {
    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    size_t start = 0;
    size_t end = res.numel();
    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0] + offset_calc(idx, res));
      scalar_t* input_dp = (scalar_t*)(data[1] + offset_calc(idx, input));
      scalar_t* tensor1_dp = (scalar_t*)(data[2] + offset_calc(idx, tensor1));
      scalar_t* tensor2_dp = (scalar_t*)(data[3] + offset_calc(idx, tensor2));
      *res_dp = functor(*input_dp, *tensor1_dp, *tensor2_dp);
    }
  }
}

// =========================================================
// Elementwise for -- Binary ops
// =========================================================

template<typename scalar_t, typename F>
inline void hb_foreach(HBTensor<scalar_t> res,
                               HBTensor<scalar_t> input,
                               HBTensor<scalar_t> other,
                               F functor) {
  char* data[3];
  data[0] = res.data_ptr();
  data[1] = input.data_ptr();
  data[2] = other.data_ptr();

  // is_trivial_1d
  if(res.ndim() == 1) {

    //-----------------------------
    // collect metadata
    //-----------------------------
    uint32_t strides[3];
    strides[0] = (res.get_strides())[0];
    strides[1] = (input.get_strides())[0];
    strides[2] = (other.get_strides())[0];

    //------------------------------
    // in the case where stride is 0
    //------------------------------
    scalar_t fixed_data[3];
    for (size_t i = 0; i < 3; i++) {
      if (strides[i] == 0) {
        fixed_data[i] = *(scalar_t*)data[i];
        data[i] = (char*)&fixed_data[i];
      }
    }

    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    size_t start = 0;
    size_t end = res.numel();
    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0]);
      scalar_t* input_dp = (scalar_t*)(data[1]);
      scalar_t* other_dp = (scalar_t*)(data[2]);
      *res_dp = functor(*input_dp, *other_dp);
      data[0] += strides[0];
      data[1] += strides[1];
      data[2] += strides[2];
    }
  } else {
    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    size_t start = 0;
    size_t end = res.numel();
    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0] + offset_calc(idx, res));
      scalar_t* input_dp = (scalar_t*)(data[1] + offset_calc(idx, input));
      scalar_t* other_dp = (scalar_t*)(data[2] + offset_calc(idx, other));
      *res_dp = functor(*input_dp, *other_dp);
    }
  }
}

// =========================================================
// Elementwise for -- Unary ops
// =========================================================

template<typename scalar_t, typename F>
inline void hb_foreach(HBTensor<scalar_t> res,
                               HBTensor<scalar_t> input,
                               F functor) {
  char* data[2];
  data[0] = res.data_ptr();
  data[1] = input.data_ptr();

  // is_trivial_1d
  if(res.ndim() == 1) {

    //-----------------------------
    // collect metadata
    //-----------------------------
    uint32_t strides[2];
    strides[0] = (res.get_strides())[0];
    strides[1] = (input.get_strides())[0];

    //------------------------------
    // in the case where stride is 0
    //------------------------------
    scalar_t fixed_data[2];
    for (size_t i = 0; i < 2; i++) {
      if (strides[i] == 0) {
        fixed_data[i] = *(scalar_t*)data[i];
        data[i] = (char*)&fixed_data[i];
      }
    }

    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    size_t start = 0;
    size_t end = res.numel();
    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0]);
      scalar_t* input_dp = (scalar_t*)(data[1]);
      *res_dp = functor(*input_dp);
      data[0] += strides[0];
      data[1] += strides[1];
    }
  } else {
    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    size_t start = 0;
    size_t end = res.numel();
    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0] + offset_calc(idx, res));
      scalar_t* input_dp = (scalar_t*)(data[1] + offset_calc(idx, input));
      *res_dp = functor(*input_dp);
    }
  }
}

// =========================================================
// Elementwise for -- Nullary ops
// =========================================================

template<typename scalar_t, typename F>
inline void hb_foreach(HBTensor<scalar_t> res,
                               F functor) {
  char* data[1];
  data[0] = res.data_ptr();

  // is_trivial_1d
  if(res.ndim() == 1) {

    //-----------------------------
    // collect metadata
    //-----------------------------
    uint32_t strides[1];
    strides[0] = (res.get_strides())[0];

    //------------------------------
    // in the case where stride is 0
    //------------------------------
    scalar_t fixed_data[1];
    for (size_t i = 0; i < 1; i++) {
      if (strides[i] == 0) {
        fixed_data[i] = *(scalar_t*)data[i];
        data[i] = (char*)&fixed_data[i];
      }
    }

    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    size_t start = 0;
    size_t end = res.numel();
    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0]);
      *res_dp = functor();
      data[0] += strides[0];
    }
  } else {
    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    size_t start = 0;
    size_t end = res.numel();
    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0] + offset_calc(idx, res));
      *res_dp = functor();
    }
  }
}

// =========================================================
// Tile Pointwise for -- Ternary
// =========================================================

template<typename scalar_t, typename F>
inline void hb_tiled_foreach(HBTensor<scalar_t> res,
                                HBTensor<scalar_t> input,
                                HBTensor<scalar_t> tensor1,
                                HBTensor<scalar_t> tensor2,
                                F functor) {
  char* data[4];
  data[0] = res.data_ptr();
  data[1] = input.data_ptr();
  data[2] = tensor1.data_ptr();
  data[3] = tensor2.data_ptr();

  // is_trivial_1d
  if(res.ndim() == 1) {

    //-----------------------------
    // collect metadata
    //-----------------------------
    uint32_t strides[4];
    strides[0] = (res.get_strides())[0];
    strides[1] = (input.get_strides())[0];
    strides[2] = (tensor1.get_strides())[0];
    strides[3] = (tensor2.get_strides())[0];

    //------------------------------
    // in the case where stride is 0
    //------------------------------
    scalar_t fixed_data[4];
    for (size_t i = 0; i < 4; i++) {
      if (strides[i] == 0) {
        fixed_data[i] = *(scalar_t*)data[i];
        data[i] = (char*)&fixed_data[i];
      }
    }

    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    hb_range range;
    calc_range(&range, res.numel());
    size_t start = range.start;
    size_t end   = range.end;

    data[0] += strides[0] * start;
    data[1] += strides[1] * start;
    data[2] += strides[2] * start;
    data[3] += strides[3] * start;
    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0]);
      scalar_t* input_dp = (scalar_t*)(data[1]);
      scalar_t* tensor1_dp = (scalar_t*)(data[2]);
      scalar_t* tensor2_dp = (scalar_t*)(data[3]);
      *res_dp = functor(*input_dp, *tensor1_dp, *tensor2_dp);
      data[0] += strides[0];
      data[1] += strides[1];
      data[2] += strides[2];
      data[3] += strides[3];
    }
  } else {
    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    hb_range range;
    calc_range(&range, res.numel());
    size_t start = range.start;
    size_t end   = range.end;

    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0] + offset_calc(idx, res));
      scalar_t* input_dp = (scalar_t*)(data[1] + offset_calc(idx, input));
      scalar_t* tensor1_dp = (scalar_t*)(data[2] + offset_calc(idx, tensor1));
      scalar_t* tensor2_dp = (scalar_t*)(data[3] + offset_calc(idx, tensor2));
      *res_dp = functor(*input_dp, *tensor1_dp, *tensor2_dp);
    }
  }
}

// =========================================================
// Tile Element-wise for -- Binary ops
//
// This function calculates the per tile range automatically
//==========================================================

template<typename scalar_t, typename F>
inline void hb_tiled_foreach(HBTensor<scalar_t> res,
                               HBTensor<scalar_t> input,
                               HBTensor<scalar_t> other,
                               F functor) {
  char* data[3];
  data[0] = res.data_ptr();
  data[1] = input.data_ptr();
  data[2] = other.data_ptr();

  // is_trivial_1d
  if(res.ndim() == 1) {

    //-----------------------------
    // collect metadata
    //-----------------------------
    uint32_t strides[3];
    strides[0] = (res.get_strides())[0];
    strides[1] = (input.get_strides())[0];
    strides[2] = (other.get_strides())[0];

    //------------------------------
    // in the case where stride is 0
    //------------------------------
    scalar_t fixed_data[3];
    for (size_t i = 0; i < 3; i++) {
      if (strides[i] == 0) {
        fixed_data[i] = *(scalar_t*)data[i];
        data[i] = (char*)&fixed_data[i];
      }
    }

    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    hb_range range;
    calc_range(&range, res.numel());
    size_t start = range.start;
    size_t end   = range.end;

    data[0] += strides[0] * start;
    data[1] += strides[1] * start;
    data[2] += strides[2] * start;
    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0]);
      scalar_t* input_dp = (scalar_t*)(data[1]);
      scalar_t* other_dp = (scalar_t*)(data[2]);
      *res_dp = functor(*input_dp, *other_dp);
      data[0] += strides[0];
      data[1] += strides[1];
      data[2] += strides[2];
    }
  } else {
    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    hb_range range;
    calc_range(&range, res.numel());
    size_t start = range.start;
    size_t end   = range.end;

    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0] + offset_calc(idx, res));
      scalar_t* input_dp = (scalar_t*)(data[1] + offset_calc(idx, input));
      scalar_t* other_dp = (scalar_t*)(data[2] + offset_calc(idx, other));
      *res_dp = functor(*input_dp, *other_dp);
    }
  }
}

// =========================================================
// Tile Element-wise for -- Unary ops
//
// This function calculates the per tile range automatically
//==========================================================

template<typename scalar_t, typename F>
inline void hb_tiled_foreach(HBTensor<scalar_t> res,
                               HBTensor<scalar_t> input,
                               F functor) {
  char* data[2];
  data[0] = res.data_ptr();
  data[1] = input.data_ptr();

  // is_trivial_1d
  if(res.ndim() == 1) {

    //-----------------------------
    // collect metadata
    //-----------------------------
    uint32_t strides[2];
    strides[0] = (res.get_strides())[0];
    strides[1] = (input.get_strides())[0];

    //------------------------------
    // in the case where stride is 0
    //------------------------------
    scalar_t fixed_data[2];
    for (size_t i = 0; i < 2; i++) {
      if (strides[i] == 0) {
        fixed_data[i] = *(scalar_t*)data[i];
        data[i] = (char*)&fixed_data[i];
      }
    }

    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    hb_range range;
    calc_range(&range, res.numel());
    size_t start = range.start;
    size_t end   = range.end;

    data[0] += strides[0] * start;
    data[1] += strides[1] * start;
    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0]);
      scalar_t* input_dp = (scalar_t*)(data[1]);
      *res_dp = functor(*input_dp);
      data[0] += strides[0];
      data[1] += strides[1];
    }
  } else {
    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    hb_range range;
    calc_range(&range, res.numel());
    size_t start = range.start;
    size_t end   = range.end;

    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0] + offset_calc(idx, res));
      scalar_t* input_dp = (scalar_t*)(data[1] + offset_calc(idx, input));
      *res_dp = functor(*input_dp);
    }
  }
}

// =========================================================
// Tile Element-wise for -- Unary ops -- Special conversion
//
// This function calculates the per tile range automatically
//==========================================================

template<typename scalar_src, typename scalar_dst, typename F>
inline void hb_tiled_foreach_conversion(HBTensor<scalar_dst> res,
                               HBTensor<scalar_src> input,
                               F functor) {
  char* data[2];
  data[0] = res.data_ptr();
  data[1] = input.data_ptr();

  // is_trivial_1d
  if(res.ndim() == 1) {

    //-----------------------------
    // collect metadata
    //-----------------------------
    uint32_t strides[2];
    strides[0] = (res.get_strides())[0];
    strides[1] = (input.get_strides())[0];

    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    hb_range range;
    calc_range(&range, res.numel());
    size_t start = range.start;
    size_t end   = range.end;

    data[0] += strides[0] * start;
    data[1] += strides[1] * start;
    size_t idx = start;
    if (end - start > 4) {
      for (; idx < end - 4; idx += 4) {
        scalar_src input_dp_0 = *(scalar_src*)(data[1]);
        scalar_dst* res_dp_0 = (scalar_dst*)(data[0]);
        data[0] += strides[0];
        data[1] += strides[1];

        scalar_src input_dp_1 = *(scalar_src*)(data[1]);
        scalar_dst* res_dp_1 = (scalar_dst*)(data[0]);
        data[0] += strides[0];
        data[1] += strides[1];

        scalar_src input_dp_2 = *(scalar_src*)(data[1]);
        scalar_dst* res_dp_2 = (scalar_dst*)(data[0]);
        data[0] += strides[0];
        data[1] += strides[1];

        scalar_src input_dp_3 = *(scalar_src*)(data[1]);
        scalar_dst* res_dp_3 = (scalar_dst*)(data[0]);
        data[0] += strides[0];
        data[1] += strides[1];

        *res_dp_0 = functor(input_dp_0);
        *res_dp_1 = functor(input_dp_1);
        *res_dp_2 = functor(input_dp_2);
        *res_dp_3 = functor(input_dp_3);
      }
    }
    for (; idx < end; idx++) {
      scalar_dst* res_dp = (scalar_dst*)(data[0]);
      scalar_src* input_dp = (scalar_src*)(data[1]);
      *res_dp = functor(*input_dp);
      data[0] += strides[0];
      data[1] += strides[1];
    }
  } else if (res.ndim() == 2) {
    // the idea is each tile takes care of the first dim in one shot
    hb_range range;
    calc_range(&range, res.dim(0));
    size_t start = range.start;
    size_t end   = range.end;

    uint32_t* src_strides = input.get_strides();
    uint32_t* src_sizes = input.get_sizes();
    uint32_t* dst_strides = res.get_strides();
    uint32_t* dst_sizes = res.get_sizes();

    for (size_t idx = start; idx < end; idx++) {
      char* dst_data = data[0] + idx * dst_strides[0];
      char* src_data = data[1] + idx * src_strides[0];

      for (size_t inner = 0; inner < res.dim(1); inner++) {
        scalar_src input_dp_0 = *(scalar_src*)(src_data);
        scalar_dst* res_dp_0 = (scalar_dst*)(dst_data);
        dst_data += dst_strides[1];
        src_data += src_strides[1];

        *res_dp_0 = functor(input_dp_0);
      }
    }
  } else if (res.ndim() == 3) {
    hb_range range;
    calc_range(&range, res.dim(0) * res.dim(1));
    size_t start = range.start;
    size_t end   = range.end;

    uint32_t* src_strides = input.get_strides();
    uint32_t* src_sizes = input.get_sizes();
    uint32_t* dst_strides = res.get_strides();
    uint32_t* dst_sizes = res.get_sizes();

    for (size_t idx = start; idx < end; idx++) {
      char* dst_data = data[0] + idx % dst_sizes[1] * dst_strides[1] + idx / dst_sizes[1] * dst_strides[0];
      char* src_data = data[1] + idx % src_sizes[1] * src_strides[1] + idx / src_sizes[1] * src_strides[0];

      for (size_t inner = 0; inner < res.dim(2); inner++) {
        scalar_src input_dp_0 = *(scalar_src*)(src_data);
        scalar_dst* res_dp_0 = (scalar_dst*)(dst_data);
        dst_data += dst_strides[2];
        src_data += src_strides[2];

        *res_dp_0 = functor(input_dp_0);
      }
    }
  } else {
    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    hb_range range;
    calc_range(&range, res.numel());
    size_t start = range.start;
    size_t end   = range.end;

    for (size_t idx = start; idx < end; idx++) {
      scalar_dst* res_dp = (scalar_dst*)(data[0] + offset_calc(idx, res));
      scalar_src* input_dp = (scalar_src*)(data[1] + offset_calc(idx, input));
      *res_dp = functor(*input_dp);
    }
  }
}

// =========================================================
// Tile Element-wise for -- Nullary ops
//
// This function calculates the per tile range automatically
//==========================================================

template<typename scalar_t, typename F>
inline void hb_tiled_foreach(HBTensor<scalar_t> res,
                               F functor) {
  char* data[1];
  data[0] = res.data_ptr();

  // is_trivial_1d
  if(res.ndim() == 1) {

    //-----------------------------
    // collect metadata
    //-----------------------------
    uint32_t strides[1];
    strides[0] = (res.get_strides())[0];

    //------------------------------
    // in the case where stride is 0
    //------------------------------
    scalar_t fixed_data[1];
    for (size_t i = 0; i < 1; i++) {
      if (strides[i] == 0) {
        fixed_data[i] = *(scalar_t*)data[i];
        data[i] = (char*)&fixed_data[i];
      }
    }

    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    hb_range range;
    calc_range(&range, res.numel());
    size_t start = range.start;
    size_t end   = range.end;

    data[0] += strides[0] * start;
    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0]);
      *res_dp = functor();
      data[0] += strides[0];
    }
  } else {
    //-----------------------------
    // iterating over all elementes
    //-----------------------------
    hb_range range;
    calc_range(&range, res.numel());
    size_t start = range.start;
    size_t end   = range.end;

    for (size_t idx = start; idx < end; idx++) {
      scalar_t* res_dp = (scalar_t*)(data[0] + offset_calc(idx, res));
      *res_dp = functor();
    }
  }
}

// =========================================================
// HB for
// =========================================================
// functor takes in current index

template <class FetchFunctor>
inline void hb_for(size_t numel, FetchFunctor functor) {
  //--------------------------------------
  // calculate start and end for this tile
  //--------------------------------------
  size_t start = 0;
  size_t end = numel;
  //-----------------
  // loop
  //----------------
  for (size_t i = start; i < end; i++) {
    functor(i);
  }
}

// =========================================================
// HB tile for
// =========================================================
// functor takes in current index

template <class FetchFunctor>
inline void hb_tiled_for(size_t numel, FetchFunctor functor) {
  //--------------------------------------
  // calculate start and end for this tile
  //--------------------------------------
  hb_range range;
  calc_range(&range, numel);
  size_t start = range.start;
  size_t end   = range.end;

  //-----------------
  // loop
  //----------------
  for (size_t i = start; i < end; i++) {
    functor(i);
  }
}

// =========================================================
// HB tile range
// =========================================================
// functor takes in current index

template <class FetchFunctor>
inline void hb_tiled_range(size_t numel, FetchFunctor functor) {
  //--------------------------------------
  // calculate start and end for this tile
  //--------------------------------------
  hb_range range;
  calc_range(&range, numel);
  size_t start = range.start;
  size_t end   = range.end;

  //-----------------
  // range
  //----------------
  functor(start, end);
}


/*
template<int N, typename scalar_t, typename F>
struct Unroll {
  inline static void copy_from(HBTensor<scalar_t> src, scalar_t* dest, size_t i);
  inline static void copy_to(scalar_t* src, HBTensor<scalar_t> dest, size_t i);
  inline static void compute(scalar_t* res, scalar_t* x, scalar_t* y, F functor);
};

template<int N, typename scalar_t, typename F>
inline void Unroll<N, scalar_t, F>::copy_from(HBTensor<scalar_t> src, scalar_t* dest, size_t i){
  dest[N] = src(i + N);
  Unroll<N-1, scalar_t, F>::copy_from(src, dest, i);
}

template<int N, typename scalar_t, typename F>
inline void Unroll<N, scalar_t, F>::copy_to(scalar_t* src, HBTensor<scalar_t> dest, size_t i){
  dest(N + i) = src[N];
  Unroll<N-1, scalar_t, F>::copy_to(src, dest, i);
}

template<int N, typename scalar_t, typename F>
inline void Unroll<N, scalar_t, F>::compute(scalar_t* res, scalar_t* x, scalar_t* y, F functor){
  res[N] = functor(x[N], y[N]);
  Unroll<N-1, scalar_t, F>::compute(res, x, y, functor);
}

template<typename scalar_t, typename F>
struct Unroll<0, scalar_t, F> {
  inline static void copy_from(HBTensor<scalar_t> src, scalar_t* dest, size_t i);
  inline static void copy_to(scalar_t* src, HBTensor<scalar_t> dest, size_t i);
  inline static void compute(scalar_t* res, scalar_t* x, scalar_t* y, F functor);
};

template<typename scalar_t, typename F>
inline void Unroll<0, scalar_t, F>::copy_from(HBTensor<scalar_t> src, scalar_t* dest, size_t i){
  dest[0] = src(i);
}

template<typename scalar_t, typename F>
inline void Unroll<0, scalar_t, F>::copy_to(scalar_t* src, HBTensor<scalar_t> dest, size_t i){
  dest(i) = src[0];
}

template<typename scalar_t, typename F>
inline void Unroll<0, scalar_t, F>::compute(scalar_t* res, scalar_t* x, scalar_t* y, F functor){
  res[0] = functor(x[0], y[0]);
}


template< int N, typename scalar_t, typename F>
inline void hb_tiled_foreach_unroll_pragma(HBTensor<scalar_t> result,
                               HBTensor<scalar_t> input,
                               HBTensor<scalar_t> other,
                               F functor) {
  scalar_t* data[3];
  data[0] = (scalar_t*)result.data_ptr();
  data[1] = (scalar_t*)input.data_ptr();
  data[2] = (scalar_t*)other.data_ptr();

  //-----------------------------
  // iterating over all elementes
  //-----------------------------
  size_t len_per_tile = result.numel() / (bsg_tiles_X * bsg_tiles_Y) + 1;
  size_t start = len_per_tile * __bsg_id;
  size_t end = start + len_per_tile;
  end = (end > result.numel())  ? result.numel() : end;

  // is_trivial_1d
  if(result.ndim() == 1) {

    //-----------------------------
    // collect metadata
    //-----------------------------
    uint32_t strides[3];
    strides[0] = (result.get_strides())[0];
    strides[1] = (input.get_strides())[0];
    strides[2] = (other.get_strides())[0];

    register scalar_t x[N];
    register scalar_t y[N];
    register scalar_t res[N];

    size_t i = start;
    while (i + N < end) {
      
      Unroll<N-1, scalar_t, F>::copy_from(data[1], y, i);
      Unroll<N-1, scalar_t, F>::copy_from(data[2], x, i);
      

      Unroll<N-1, scalar_t, F>::copy_from(input, y, i);
      Unroll<N-1, scalar_t, F>::copy_from(other, x, i);

      Unroll<N-1, scalar_t, F>::compute(res, x, y, functor);
      
      //Unroll<N-1, scalar_t, F>::copy_to(res, data[0], i);
      Unroll<N-1, scalar_t, F>::copy_to(res, result, i);

      i += N;
    }
    if (start + N < end) {
      i -= N;
    }
    while (i < end) {
      scalar_t x = *(data[1] + i * strides[1]);
      scalar_t y = *(data[2] + i * strides[2]);
      scalar_t res = functor(x, y);
      *(data[0] + strides[0]*i++) = res;
    }
  }
  /*
  else {
    size_t i = start;
    while (i < end) {
      scalar_t x = *(data[1] + offset(i, input));
      scalar_t y = *(data[2] + offset(i, other));
      scalar_t res = functor(x, y);
      *(data[0] + offset(i, result)) = res;
      i++;
    }
  } 
  
}
*/
#endif
