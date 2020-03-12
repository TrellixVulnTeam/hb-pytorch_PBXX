//====================================================================
// Element-wise add kernel
// 03/05/2020 Lin Cheng and Bandhav Veluri (lc873@cornell.edu)
//====================================================================

#include <kernel_common.hpp>
#include <brg_element_for.hpp>

// We wrap all external-facing C++ kernels with `extern "C"` to
// prevent name mangling

extern "C" {

  __attribute__ ((noinline))  int tensorlib_add(
          bsg_tensor_t* t0_p,
          bsg_tensor_t* t1_p,
          bsg_tensor_t* t2_p,
          float* _alpha) {
    float alpha = *_alpha;
    // Start profiling
    bsg_cuda_print_stat_kernel_start();
    brg_element_wise_for<float>(t0_p, t1_p, t2_p,
        [&](float a, float b) {
          return a + alpha * b;
        });
    //   End profiling
    bsg_cuda_print_stat_kernel_end();
    return 0;
  }

  HB_EMUL_REG_KERNEL(tensorlib_add, bsg_tensor_t*, bsg_tensor_t*, bsg_tensor_t*, float*)

}
