//====================================================================
// SPMD 2D Convolution
// Idea is that each tile will receive a piece of output image that
// does not overlap with any other tile to work on
// 10/02/2020 Lin Cheng
//====================================================================

#define RAW_DIM       32
#define BLOCK_DIM_X   16
#define BLOCK_DIM_Y   16
#define FILTER_DIM     3
#define PADDING        1
#define STRIDE         1

#define IMAP_DIM_X (BLOCK_DIM_X + FILTER_DIM - 1)
#define IMAP_DIM_Y (BLOCK_DIM_Y + FILTER_DIM - 1)

#include <kernel_common.hpp>
#include <kernel_conv_baseline.hpp>


extern "C" {

  __attribute__ ((noinline))  int tensorlib_conv_resnet_32_3x3(
    hb_tensor_t* output,
    hb_tensor_t* input,
    hb_tensor_t* weight,
    hb_vector_t* padding,
    hb_vector_t* strides) {

    HBTensor<float, 4> omap(output);
    HBTensor<float, 4> imap(input);
    HBTensor<float, 4> filter(weight);
    HBVector<uint32_t> p(padding);
    HBVector<uint32_t> s(strides);

    // Conv2d parameters
    auto N    = omap.dim(0); // number of images in batch
    auto Cout = omap.dim(1); // number of output channels
    auto Hout = omap.dim(2);
    auto Wout = omap.dim(3);
    auto Cin  = imap.dim(1); // number of input channels
    auto Hin  = imap.dim(2);
    auto Win  = imap.dim(3);
    auto Hk   = filter.dim(2);
    auto Wk   = filter.dim(3);

    // cross check
    hb_assert(FILTER_DIM == Hk);
    hb_assert(FILTER_DIM == Wk);
    hb_assert(RAW_DIM == Hin);  // assume we are doing 32x32 -> 32x32
    hb_assert(RAW_DIM == Win);
    hb_assert(RAW_DIM == Hout);
    hb_assert(RAW_DIM == Wout);
    hb_assert(PADDING == p[0]); // assume padding == 1
    hb_assert(PADDING == p[1]);
    hb_assert(PADDING == s[0]); // assume stride == 1
    hb_assert(PADDING == s[1]);

    hb_assert(Hout % BLOCK_DIM_Y == 0); // we dont have partial blocks
    hb_assert(Wout % BLOCK_DIM_X == 0);

    size_t h_blocks_per_out_channel = Hout / BLOCK_DIM_Y;
    size_t w_blocks_per_out_channel = Wout / BLOCK_DIM_X;

    size_t blocks_per_out_channel = h_blocks_per_out_channel * w_blocks_per_out_channel;
    size_t num_blocks = N * Cout * blocks_per_out_channel;

    // allocate buffers
    float filter_buf[FILTER_DIM * FILTER_DIM];
    float omap_buf[BLOCK_DIM_X * BLOCK_DIM_Y];
    float imap_buf[IMAP_DIM_X * IMAP_DIM_Y];


    auto filterDMA = [&](size_t filter_id, size_t channel_id) {
      float* filter_src_base = (float*)filter.data_ptr();
      uint32_t* filter_src_strides = filter.get_strides();
      filter_src_base += filter_id * filter_src_strides[0] + channel_id * filter_src_strides[1];
      fill_filter_buffer<FILTER_DIM>(filter_src_base, filter_buf);
    };

    auto imapDMA = [&](size_t image_id, size_t channel_id, size_t block_x, size_t block_y) {
      size_t imap_x = block_x * BLOCK_DIM_X;
      size_t imap_y = block_y * BLOCK_DIM_Y;
      float* imap_src_base = (float*)imap.data_ptr();
      uint32_t* imap_src_strides = imap.get_strides();
      imap_src_base += image_id * imap_src_strides[0] + channel_id * imap_src_strides[1];
      imap_src_base += imap_y * imap_src_strides[2] + imap_x * imap_src_strides[3];
      size_t y_step = imap_src_strides[2];
      fill_imap_buffer<IMAP_DIM_X, IMAP_DIM_Y>(imap_src_base, imap_buf, y_step);
    };

    // add 1 col of zeros
    auto addPaddingH_1 = [&](size_t start) {
      bsg_unroll(IMAP_DIM_Y)
      for (size_t r = 0; r < IMAP_DIM_Y; r++) {
        imap_buf[start] = 0;
        start += IMAP_DIM_X;
      }
    };

    // add 1 row of zeros
    auto addPaddingW_1 = [&](size_t start) {
      bsg_unroll(IMAP_DIM_X)
      for (size_t c = 0; c < IMAP_DIM_X; c++) {
        imap_buf[start + c] = 0;
      }
    };


    auto imapDMA_padding = [&](size_t image_id, size_t channel_id, size_t block_x, size_t block_y) {
      size_t imap_x = block_x * BLOCK_DIM_X;
      size_t imap_y = block_y * BLOCK_DIM_Y;
      // this is used to correct the padding output offset
      imap_x = imap_x == 0 ? 0 : imap_x - PADDING;
      imap_y = imap_y == 0 ? 0 : imap_y - PADDING;
      size_t logical_start = 0; // starting offset of imap buffer writting
      size_t read_x = 0;        // how many elements to read for a row
      size_t read_y = 0;        // how many rows to read
      // see if we need to add padding
      if (block_y == 0) {
        size_t imap_buf_offset = 0;
        // add top padding
        addPaddingW_1(0);
        if (block_x == 0) {
          // add left padding
          addPaddingH_1(0);
          // setup read data copy
          logical_start = PADDING*IMAP_DIM_X+PADDING;
          read_x = IMAP_DIM_X-PADDING;
          read_y = IMAP_DIM_Y-PADDING;
        }
        else if (block_x == w_blocks_per_out_channel-1) {
          // add right padding
          addPaddingH_1(IMAP_DIM_X-1);
          logical_start = PADDING*IMAP_DIM_X;
          read_x = IMAP_DIM_X-PADDING;
          read_y = IMAP_DIM_Y-PADDING;
        } else {
          // top only
          logical_start = PADDING*IMAP_DIM_X;
          read_x = IMAP_DIM_X;
          read_y = IMAP_DIM_Y-PADDING;
        }
      } else if (block_y == h_blocks_per_out_channel-1) {
        // add bottom padding
        addPaddingW_1((IMAP_DIM_Y-1)*IMAP_DIM_X);
        if (block_x == 0) {
          // add left padding
          addPaddingH_1(0);
          logical_start = PADDING;
          read_x = IMAP_DIM_X-PADDING;
          read_y = IMAP_DIM_Y-PADDING;
        }
        else if (block_x == w_blocks_per_out_channel-1) {
          // add right padding
          addPaddingH_1(IMAP_DIM_X-PADDING);
          logical_start = 0;
          read_x = IMAP_DIM_X-PADDING;
          read_y = IMAP_DIM_Y-PADDING;
        } else {
          // bottom only
          logical_start = 0;
          read_x = IMAP_DIM_X;
          read_y = IMAP_DIM_Y-PADDING;
        }
      } else if (block_x == 0) {
        // add left padding only
        addPaddingH_1(0);
        logical_start = PADDING;
        read_x = IMAP_DIM_X-PADDING;
        read_y = IMAP_DIM_Y;
      } else if (block_x == w_blocks_per_out_channel-1) {
        // add right padding only
        addPaddingH_1(IMAP_DIM_X-PADDING);
        logical_start = 0;
        read_x = IMAP_DIM_X-PADDING;
        read_y = IMAP_DIM_Y;
      } else {
        // no padding is needed
        logical_start = 0;
        read_x = IMAP_DIM_X;
        read_y = IMAP_DIM_Y;
      }
      float* imap_src_base = (float*)imap.data_ptr();
      uint32_t* imap_src_strides = imap.get_strides();
      imap_src_base += image_id * imap_src_strides[0] + channel_id * imap_src_strides[1];
      imap_src_base += imap_y * imap_src_strides[2] + imap_x * imap_src_strides[3];
      size_t y_step = imap_src_strides[2];
      for (size_t r = 0; r < read_y; r++) {
        size_t row_offset = logical_start;
        float* row_src = imap_src_base;
        for (size_t c = 0; c < read_x; c++) {
          imap_buf[row_offset] = *row_src;
          row_src++;
          row_offset++;
        }
        imap_src_base += y_step;
        logical_start += IMAP_DIM_X;
      }
      // debug
      size_t debug_offset = 0;
      for (size_t r = 0; r < IMAP_DIM_Y; r++) {
        for (size_t c = 0; c < IMAP_DIM_X; c++) {
          std::cout << imap_buf[debug_offset] << " ";
          debug_offset++;
        }
        std::cout << std::endl;
      }
      std::cout << std::endl;
      std::cout << std::endl;
    };

    auto omapDMA = [&](size_t image_id, size_t filter_id, size_t block_x, size_t block_y) {
      size_t omap_x = block_x * BLOCK_DIM_X;
      size_t omap_y = block_y * BLOCK_DIM_Y;
      float* omap_src_base = (float*)omap.data_ptr();
      uint32_t* omap_src_strides = omap.get_strides();
      omap_src_base += image_id * omap_src_strides[0] + filter_id * omap_src_strides[1];
      omap_src_base += omap_y * omap_src_strides[2] + omap_x * omap_src_strides[3];
      size_t y_step = omap_src_strides[2];
      drain_omap_buffer<BLOCK_DIM_X, BLOCK_DIM_Y>(omap_buf, omap_src_base, y_step);
    };

    bsg_cuda_print_stat_kernel_start();

    // main loop
    for (size_t idx = bsg_id; idx < num_blocks; idx += (BSG_TILE_GROUP_X_DIM * BSG_TILE_GROUP_Y_DIM)) {
      if (idx < num_blocks) {

        // figure out what we are producing
        size_t tmp = idx;
        size_t image_id = tmp / (Cout * blocks_per_out_channel);
        tmp = tmp % (Cout * blocks_per_out_channel);
        size_t filter_id = tmp / blocks_per_out_channel;
        tmp = tmp % blocks_per_out_channel;
        size_t block_y = tmp / w_blocks_per_out_channel;
        size_t block_x = tmp % w_blocks_per_out_channel;

        // reset output buffer
        reset_buffer<BLOCK_DIM_X, BLOCK_DIM_Y>(omap_buf);

        for (size_t channel_id = 0; channel_id < Cin; channel_id++) {

          // read in the image
          //imapDMA(image_id, channel_id, block_x, block_y);
          imapDMA_padding(image_id, channel_id, block_x, block_y);

          // read in the filter
          filterDMA(filter_id, channel_id);

          // do conv
          conv2d_3x3_16(imap_buf, filter_buf, omap_buf);

        } // channel

        // write omap back
        omapDMA(image_id, filter_id, block_x, block_y);

      } // if (idx < num_blocks)
    } // main loop

    bsg_cuda_print_stat_kernel_end();

    g_barrier.sync();
    return 0;
  }

  HB_EMUL_REG_KERNEL(tensorlib_conv_resnet_32_3x3, hb_tensor_t*, hb_tensor_t*, hb_tensor_t*,
                     hb_vector_t*, hb_vector_t*)

}

