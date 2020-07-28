//====================================================================
// Vector sort kernel
// 06/04/2020 Krithik Ranjan (kr397@cornell.edu)
//====================================================================

#include <kernel_common.hpp>
#include <cmath>


// Helper function to perform Quick Sort
// Modified implementation from 
// https://beginnersbook.com/2015/02/quicksort-program-in-c/
static void tensorlib_vsort_recur(
    HBTensor<float> * vec, 
    int32_t first, 
    int32_t last) {
        
    if (first >= last) {
        return;
    }
    else {
        int32_t i = first;
        int32_t j = last;
        int32_t piv = (first + last) / 2;

        while (i < j) {
            while ((*vec)(i) <= (*vec)(piv) && i < last) {
                i++;
            }
            while ((*vec)(j) > (*vec)(piv)) {
                j--;
            }

            if (i < j) {
                // Swap elements at i and j
                auto temp = (*vec)(i);
                (*vec)(i) = (*vec)(j);
                (*vec)(j) = temp;
                /*
                (*vec)(i) = (*vec)(i) + (*vec)(j);
                (*vec)(j) = (*vec)(i) - (*vec)(j);
                (*vec)(i) = (*vec)(i) - (*vec)(j);
                */
            }
        }

        // Swap elements at j and piv
        auto temp = (*vec)(piv);
        (*vec)(piv) = (*vec)(j);
        (*vec)(j) = temp;
        /*
        (*vec)(piv) = (*vec)(piv) + (*vec)(j);
        (*vec)(j) = (*vec)(piv) - (*vec)(j);
        (*vec)(piv) = (*vec)(piv) - (*vec)(j);
        */
        tensorlib_vsort_recur(vec, first, j-1);
        tensorlib_vsort_recur(vec, j+1, last);
    }
}

static void quicksort_recur(
    char* data,
    uint32_t stride, 
    int32_t first, 
    int32_t last) {
        
    if (first >= last) {
        return;
    }
    else {
        int32_t i = first;
        int32_t j = last;
        int32_t piv = (first + last) / 2;

        while (i < j) {
            auto i_val = *(float*)(data + i*stride);
            auto j_val = *(float*)(data + j*stride);
            auto p_val = *(float*)(data + piv*stride);
            

            while (i_val <= p_val && i < last) {
                i++;
            }
            while (j_val > p_val) {
                j--;
            }

            if (i < j) {
                // Swap elements at i and j
                float* v1 = (float*)(data + i*stride);
                float* v2 = (float*)(data + j*stride);
              
                float temp = *v1;
                *v1 = *v2;
                *v2 = temp;
            }
        }

        // Swap elements at j and piv
        float* v1 = (float*)(data + j*stride);
        float* v2 = (float*)(data + piv*stride);

        float temp = *v1;
        *v1 = *v2;
        *v2 = temp;
        
        quicksort_recur(data, stride, first, j-1);
        quicksort_recur(data, stride, j+1, last);
    }
}


static void merge_odd_even (
  char* data,
  uint32_t stride, 
  size_t start, 
  size_t size, 
  size_t diff
) {

  size_t m = diff * 2;

  if (m < size)
  {
    // Even subsequence
    merge_odd_even(data, stride, start, size, m);
    // Odd subsequence
    merge_odd_even(data, stride, start+diff, size, m);

    for (size_t i = start+diff; i+diff < start+size; i += m) {
      float* v1 = (float*)(data + i*stride); 
      float* v2 = (float*)(data + (i+diff)*stride);
      if (*v1 > *v2) {
        // Swap the two values
        float t = *v1;
        *v1 = *v2;
        *v2 = t;
      }
    }
  }
  else {
    float* v1 = (float*)(data + start*stride); 
    float* v2 = (float*)(data + (start+diff)*stride);
    if (*v1 > *v2) {
      // Swap the two values
      float t = *v1;
      *v1 = *v2;
      *v2 = t;
    }
  }
}


static void merge_sort (
  char* data,
  uint32_t stride,
  size_t start, 
  size_t end
) {
  if (start < end) {
    size_t mid = start + (end - start)/2;

    // Sort both halves
    merge_sort(data, stride, start, mid);
    merge_sort(data, stride, mid+1, end);

    size_t size = end - start;

    // Merge the two halves
    merge_odd_even(data, stride, start, size, 1);
  }
}

static void compare_swap(HBTensor<float>* vec, size_t i, size_t j) 
{
  if (i < (*vec).numel() && j < (*vec).numel()) {
    if ((*vec)(i) > (*vec)(j)) {
      auto t = (*vec)(i);
      (*vec)(i) = (*vec)(j);
      (*vec)(j) = t;
    }
  }
}

static void odd_even(HBTensor<float>* vec, size_t l, size_t s, size_t r)
{
  size_t m  = r * 2;
  if (m < s) {
    // Odd series
    odd_even(vec, l, s, m);
    // Even series
    odd_even(vec, l+r, s, m);
    for (size_t i = l+r; i+r < l+s; i += m) {
      if (i+r < (*vec).numel()) {
        compare_swap(vec, i, i+r);
      }
    }
  }
  else {
    if (l+r < (*vec).numel()) {
      compare_swap(vec, l, l+r);  
    }
  }
}

static void merge_recur(HBTensor<float>* vec, size_t div)
{
  size_t lo = __bsg_id * div;
  size_t hi = lo + div;
  if (lo < (*vec).numel()) {
    odd_even(vec, lo, div, 1);
  }
}

static void merge_range(HBTensor<float>* vec, size_t lo, size_t hi){
  if (hi - lo > 1) {
    size_t mid = lo + ((hi - lo) / 2);
    merge_range(vec, lo, mid);
    merge_range(vec, mid, hi);
    odd_even(vec, lo, hi-lo, 1);
  }
}

extern "C" {

  __attribute__ ((noinline))  int tensorlib_vsort(
          hb_tensor_t* result_p,
          hb_tensor_t* self_p) {

    // Convert low level pointers to Tensor objects
    HBTensor<float> result(result_p);
    HBTensor<float> self(self_p);

    // Start profiling
    bsg_cuda_print_stat_kernel_start();

  
    /*
    // Copy all elements to result
    hb_tiled_foreach(result, self,
      [=](float a) {
        return a;
    });
    */

    if (self.ndim() == 1) {
      
      uint16_t bsg_total = bsg_tiles_X * bsg_tiles_Y;

      size_t total_div = result.numel();
      if (ceil(log2(total_div)) != floor(log2(total_div))){
        total_div = pow(2, ceil(log2(total_div)));
      }

      // Each tile can start with two elements
      if (bsg_total >= ((float)result.numel() / 2)) {
        // Copy over the assigned tensor elements
        if (__bsg_id*2 < result.numel()){
          result(__bsg_id*2) = self(__bsg_id*2);
        }
        if (__bsg_id*2 + 1 < result.numel()){
          result(__bsg_id*2+1) = self(__bsg_id*2+1);
        }

        size_t div = 2;
        while (div <= total_div) { //result.numel()) {
          g_barrier.sync();
          merge_recur(&result, div);
          div *= 2;
        }
      }
      else {
        float len_tile = (float) result.numel() / bsg_total;
        if (ceil(log2(len_tile)) != floor(log2(len_tile))){
          len_tile = pow(2, ceil(log2(len_tile)));
        }
        
        size_t lo = __bsg_id * len_tile;
        size_t hi = lo + len_tile;
        if (lo < result.numel()) {
          // Copy over the assigned elements 
          for (size_t i = lo; i < hi && i < result.numel(); i++) {
            result(i) = self(i);
          }
          merge_range(&result, lo, hi);
        }

        size_t div = len_tile*2;
        while (div <= total_div) {
          g_barrier.sync();
          merge_recur(&result, div);
          div *= 2;
        }
        
      }
    }
    else {
      uint16_t total_vec = 1;
      for (int i = 0; i < self.ndim(); i++)
      {
        total_vec *= self.dim(i);
      }

      uint16_t bsg_total = bsg_tiles_X * bsg_tiles_Y;
      if (bsg_total > total_vec) {
        uint16_t tile_per_vec = bsg_total / total_vec;


      }
      else {
        uint16_t vec_per_tile = total_vec / bsg_total;

        
      }
    }

    //   End profiling
    bsg_cuda_print_stat_kernel_end();

    // Sync
    g_barrier.sync();
    return 0;
  }

  // Register the HB kernel with emulation layer
  HB_EMUL_REG_KERNEL(tensorlib_vsort, hb_tensor_t*, hb_tensor_t*)

}
