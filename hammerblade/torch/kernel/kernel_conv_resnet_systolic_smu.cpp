//====================================================================
// SPMD 2D Convolution using SMU
// Idea is that each tile will receive a piece of output image that
// does not overlap with any other tile to work on
// 12/08/2020 Lin Cheng, Peitian Pan
//====================================================================

#define RAW_DIM       32
#define BLOCK_DIM_X   16
#define BLOCK_DIM_Y    8
#define FILTER_DIM     3
#define PADDING        1
#define STRIDE         1
#define BUFFERS        2

#define IMAP_DIM_X (BLOCK_DIM_X + FILTER_DIM - 1)
#define IMAP_DIM_Y (BLOCK_DIM_Y + FILTER_DIM - 1)

#include <kernel_common.hpp>
#include <kernel_conv_baseline.hpp>
#include <kernel_circular_buffer.hpp>
#include <hb_smu.hpp>

typedef CircularBuffer::FIFO<float, IMAP_DIM_X*IMAP_DIM_Y, BUFFERS> DoubleBuffer;
typedef CircularBuffer::FIFO<float, BLOCK_DIM_X*BLOCK_DIM_Y, BUFFERS> GradDoubleBuffer;
typedef HBTensor<float, 4> ConvTensor;

namespace{
inline void spcpy_imap(bsg_attr_remote float* dest, float* src) {
  // bsg_unroll(IMAP_DIM_Y)
  // for (int i = 0; i < IMAP_DIM_X * IMAP_DIM_Y; i++) {
  //   dest[i] = src[i];
  // }
  for (int i = 0; i < IMAP_DIM_X * IMAP_DIM_Y; i += 9) {
    register float tmp0 = *(src + 0);
    register float tmp1 = *(src + 1);
    register float tmp2 = *(src + 2);
    register float tmp3 = *(src + 3);
    register float tmp4 = *(src + 4);
    register float tmp5 = *(src + 5);
    register float tmp6 = *(src + 6);
    register float tmp7 = *(src + 7);
    register float tmp8 = *(src + 8);
    asm volatile("": : :"memory");
    *(dest + 0) = tmp0;
    *(dest + 1) = tmp1;
    *(dest + 2) = tmp2;
    *(dest + 3) = tmp3;
    *(dest + 4) = tmp4;
    *(dest + 5) = tmp5;
    *(dest + 6) = tmp6;
    *(dest + 7) = tmp7;
    *(dest + 8) = tmp8;
    src += 9;
    dest += 9;
  }
}

inline void spcpy_grad(bsg_attr_remote float* dest, float* src) {
  // bsg_unroll(BLOCK_DIM_Y)
  // for (int i = 0; i < BLOCK_DIM_X * BLOCK_DIM_Y; i++) {
  //   dest[i] = src[i];
  // }
  for (int i = 0; i < BLOCK_DIM_X * BLOCK_DIM_Y; i += 8) {
    register float tmp0 = *(src + 0);
    register float tmp1 = *(src + 1);
    register float tmp2 = *(src + 2);
    register float tmp3 = *(src + 3);
    register float tmp4 = *(src + 4);
    register float tmp5 = *(src + 5);
    register float tmp6 = *(src + 6);
    register float tmp7 = *(src + 7);
    asm volatile("": : :"memory");
    *(dest + 0) = tmp0;
    *(dest + 1) = tmp1;
    *(dest + 2) = tmp2;
    *(dest + 3) = tmp3;
    *(dest + 4) = tmp4;
    *(dest + 5) = tmp5;
    *(dest + 6) = tmp6;
    *(dest + 7) = tmp7;
    src += 8;
    dest += 8;
  }
}

inline void
loop_inc(size_t image_id, size_t filter_id, size_t channel_id,
         size_t& image_id_nxt, size_t& filter_id_nxt, size_t& channel_id_nxt,
         int N, int Cout, int Cin) {
  image_id_nxt = image_id;
  filter_id_nxt = filter_id;
  channel_id_nxt = channel_id+1;
  if ( channel_id_nxt >= Cin ) {
    channel_id_nxt = 0;
    filter_id_nxt = filter_id+16;
    if ( filter_id_nxt >= Cout) {
      filter_id_nxt = 0;
      image_id_nxt = image_id+1;
      if ( image_id_nxt >= N ) {
        image_id_nxt = 0;
      }
    }
  }
}

inline void
loop_inc_back_weight(
    size_t filter_id, size_t channel_id, size_t image_id,
    size_t block_y, size_t block_x,
    size_t& filter_id_nxt, size_t& channel_id_nxt, size_t& image_id_nxt,
    size_t& block_y_nxt, size_t& block_x_nxt,
    int N, int Cout, int N_imap,
    int h_blocks_per_out_channel, int w_blocks_per_out_channel) {
  filter_id_nxt = filter_id;
  channel_id_nxt = channel_id;
  image_id_nxt = image_id;
  block_y_nxt = block_y;
  block_x_nxt = block_x+1;
  if ( block_x_nxt >= w_blocks_per_out_channel ) {
    block_x_nxt = 0;
    block_y_nxt = block_y+1;
    if ( block_y_nxt >= h_blocks_per_out_channel ) {
      block_y_nxt = 0;
      image_id_nxt = image_id+1;
      if ( image_id_nxt >= N_imap ) {
        image_id_nxt = 0;
        channel_id_nxt = channel_id+8;
        if ( channel_id_nxt >= Cout ) {
          channel_id_nxt = bsg_y;
          filter_id_nxt = filter_id+16;
          if ( filter_id_nxt >= N ) {
            filter_id_nxt = bsg_x;
          }
        }
      }
    }
  }
}

} // namespace

inline void
load_conv_imap_back_weight(
    ConvTensor& src,
    size_t filter_id, size_t channel_id, size_t image_id,
    size_t block_y, size_t block_x,
    int N, int Cout, int N_imap,
    int h_blocks_per_out_channel, int w_blocks_per_out_channel,
    int* ack, DoubleBuffer& fifo ) {

  size_t filter_id_nxt, channel_id_nxt, image_id_nxt;
  size_t block_y_nxt, block_x_nxt;

  loop_inc_back_weight(
      filter_id, channel_id, image_id, block_y, block_x,
      filter_id_nxt, channel_id_nxt, image_id_nxt, block_y_nxt, block_x_nxt,
      N, Cout, N_imap, h_blocks_per_out_channel, w_blocks_per_out_channel );

  bool is_first_call = (filter_id == bsg_x) && (channel_id == bsg_y)
                    && (image_id == 0) && (block_y == 0) && (block_x == 0);

  if ( is_first_call ) {
    // First time loading imap
    launch_smu_conv_imap(
        block_x, block_y,
        image_id, filter_id, channel_id,
        src,
        fifo.get_buffer(), ack,
        BLOCK_DIM_X, BLOCK_DIM_Y,
        FILTER_DIM, PADDING );
    wait_smu( ack );
    // Load second block. Wait for ACK in the next call
    launch_smu_conv_imap(
        block_x, block_y,
        image_id_nxt, filter_id_nxt, channel_id_nxt,
        src,
        fifo.get_next_buffer(), ack,
        BLOCK_DIM_X, BLOCK_DIM_Y,
        FILTER_DIM, PADDING );
  } else {
    // If not first call, wait till SMU acks
    wait_smu( ack );
    // Loading one extra block doesn't impact obserable kernel cycle count
    // One FIFO has not been loaded; call SMU to load data
    launch_smu_conv_imap(
        block_x, block_y,
        image_id_nxt, filter_id_nxt, channel_id_nxt,
        src,
        fifo.get_next_buffer(), ack,
        BLOCK_DIM_X, BLOCK_DIM_Y,
        FILTER_DIM, PADDING );
  }

  // Tell the buffer that the SMU has finished a pull-based write
  fifo.SMU_finish_wb();

} // load_conv_imap_back_weight

inline void
load_conv_grad_back_weight(
    ConvTensor& src,
    size_t filter_id, size_t channel_id, size_t image_id,
    size_t block_y, size_t block_x,
    int N, int Cout, int N_imap,
    int h_blocks_per_out_channel, int w_blocks_per_out_channel,
    int* ack, GradDoubleBuffer& fifo ) {

  size_t filter_id_nxt, channel_id_nxt, image_id_nxt;
  size_t block_y_nxt, block_x_nxt;

  loop_inc_back_weight(
      filter_id, channel_id, image_id, block_y, block_x,
      filter_id_nxt, channel_id_nxt, image_id_nxt, block_y_nxt, block_x_nxt,
      N, Cout, N_imap, h_blocks_per_out_channel, w_blocks_per_out_channel );

  bool is_first_call = (filter_id == bsg_x) && (channel_id == bsg_y)
                    && (image_id == 0) && (block_y == 0) && (block_x == 0);

  if ( is_first_call ) {
    // First time loading imap
    launch_smu_conv_grad(
        block_x, block_y,
        image_id, filter_id, channel_id,
        src,
        fifo.get_buffer(), ack,
        BLOCK_DIM_X, BLOCK_DIM_Y,
        FILTER_DIM, PADDING );
    wait_smu( ack );
    // Load second block. Wait for ACK in the next call
    launch_smu_conv_grad(
        block_x, block_y,
        image_id_nxt, filter_id_nxt, channel_id_nxt,
        src,
        fifo.get_next_buffer(), ack,
        BLOCK_DIM_X, BLOCK_DIM_Y,
        FILTER_DIM, PADDING );
  } else {
    // If not first call, wait till SMU acks
    wait_smu( ack );
    // Loading one extra block doesn't impact obserable kernel cycle count
    // One FIFO has not been loaded; call SMU to load data
    launch_smu_conv_grad(
        block_x, block_y,
        image_id_nxt, filter_id_nxt, channel_id_nxt,
        src,
        fifo.get_next_buffer(), ack,
        BLOCK_DIM_X, BLOCK_DIM_Y,
        FILTER_DIM, PADDING );
  }

  // Tell the buffer that the SMU has finished a pull-based write
  fifo.SMU_finish_wb();

} // load_conv_grad_back_weight

inline void
load_conv_imap( ConvTensor& src, size_t block_x, size_t block_y,
                size_t image_id, size_t filter_id, size_t channel_id,
                int N, int Cout, int Cin,
                int* ack,
                DoubleBuffer& fifo ) {

  size_t image_id_nxt, filter_id_nxt, channel_id_nxt;

  loop_inc( image_id, filter_id, channel_id,
            image_id_nxt, filter_id_nxt, channel_id_nxt,
            N, Cout, Cin );

  if ( (image_id == 0) && (filter_id == 0) && (channel_id == 0) ) {
    // First time loading imap
    launch_smu_conv_imap(
        block_x, block_y,
        image_id, filter_id, channel_id,
        src,
        fifo.get_buffer(), ack,
        BLOCK_DIM_X, BLOCK_DIM_Y,
        FILTER_DIM, PADDING );
    wait_smu( ack );
    // Load second block. Wait for ACK in the next call
    launch_smu_conv_imap(
        block_x, block_y,
        image_id_nxt, filter_id_nxt, channel_id_nxt,
        src,
        fifo.get_next_buffer(), ack,
        BLOCK_DIM_X, BLOCK_DIM_Y,
        FILTER_DIM, PADDING );
  } else {
    // If not first call, wait till SMU acks
    wait_smu( ack );
    // Loading one extra block doesn't impact obserable kernel cycle count
    // One FIFO has not been loaded; call SMU to load data
    launch_smu_conv_imap(
        block_x, block_y,
        image_id_nxt, filter_id_nxt, channel_id_nxt,
        src,
        fifo.get_next_buffer(), ack,
        BLOCK_DIM_X, BLOCK_DIM_Y,
        FILTER_DIM, PADDING );
  }

  // Tell the buffer that the SMU has finished a pull-based write
  fifo.SMU_finish_wb();

} // load_conv_imap

extern "C" {

  __attribute__ ((noinline))  int tensorlib_conv_resnet_32_3x3_32x32_smu(
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
    float* imap_buf;

    // Buffer
    CircularBuffer::FIFO<float, IMAP_DIM_X * IMAP_DIM_Y, BUFFERS> fifo(bsg_y, bsg_x-1, bsg_y, bsg_x+1);

    // ACK variable
    int ack = 0;

    // Config
    // 0 -- idle
    // 1 -- imap DMA
    // 2 -- compute

    // the array is divided into 3 32 (8x4) blocks
    // if (x+1) % 5 == 0 -- do not write to the right
    // 8 rows handle one image collectively
    // 4 columns each on 1 filter
    // one row is one sub block
    // one col is one filter
    /* char systolic_resnet[8][16] = { */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /* }; */

    /* char tile_config = systolic_resnet[bsg_y][bsg_x]; */

    bool should_pass = bsg_x == 15 ? false : true;

    // Job dispatch

    // Since the given imap is of 32x32 and the manycore has 16x8 array,
    // allocate 16 columns for column 0 and 16 for column 1. Similarly
    // allocate 2 rows for row 0, row 1, row 2, and row 3, respectively.
    size_t block_y = bsg_y / 2;
    size_t block_x = bsg_y % 2;

    auto filterDMA = [&](size_t filter_id, size_t channel_id) {
      float* filter_src_base = (float*)filter.data_ptr();
      uint32_t* filter_src_strides = filter.get_strides();
      filter_src_base += filter_id * filter_src_strides[0] + channel_id * filter_src_strides[1];
      fill_filter_buffer<FILTER_DIM>(filter_src_base, filter_buf);
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

    bool is_first_col = bsg_x == 0;

    auto compute_job = [&](size_t block_x, size_t block_y) {
      size_t filter_offset = bsg_x;

      for (size_t image_id = 0; image_id < N; image_id++) {
        // 4 filter per block, 3 blocks
        for (size_t filter_id = filter_offset; filter_id < Cout; filter_id += 16) {
          if (filter_id < Cout) {
            // reset output buffer
            reset_buffer<BLOCK_DIM_X, BLOCK_DIM_Y>(omap_buf);
            for (size_t channel_id = 0; channel_id < Cin; channel_id++) {

              if ( is_first_col ) {
                load_conv_imap( imap, block_x, block_y,
                                image_id, filter_id, channel_id,
                                N, Cout, Cin,
                                &ack,
                                fifo );
              }

              imap_buf = fifo.obtain_rd_ptr();

              if (should_pass && filter_id+1 < Cout) {
                float* imap_buf_remote = fifo.obtain_wr_ptr();
                spcpy_imap(imap_buf_remote, imap_buf);
                fifo.finish_wr_ptr();
              }

              // read in the filter
              filterDMA(filter_id, channel_id);

              // do conv
              conv2d_3x3_16<BLOCK_DIM_X, BLOCK_DIM_Y, IMAP_DIM_X, IMAP_DIM_Y, FILTER_DIM>(imap_buf, filter_buf, omap_buf);

              fifo.finish_rd_ptr();
            } // channel

            // write omap back
            omapDMA(image_id, filter_id, block_x, block_y);

          } // main loop
        }
      }
    };

    // put a sync after init ... otherwise we can deadlock
    g_barrier.sync();
    bsg_cuda_print_stat_start(7);

    compute_job( block_x, block_y );

    bsg_cuda_print_stat_end(7);
    g_barrier.sync();

    return 0;
  }




  __attribute__ ((noinline))  int tensorlib_conv_resnet_32_3x3_32x32_back_input_smu(
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

    // XXX: in the backward pass for input, we need to pad 3 - 1 - 1 = 1 ...
    //      so it happens to be the same as forward pass

    hb_assert(Hout % BLOCK_DIM_Y == 0); // we dont have partial blocks
    hb_assert(Wout % BLOCK_DIM_X == 0);

    size_t h_blocks_per_out_channel = Hout / BLOCK_DIM_Y;
    size_t w_blocks_per_out_channel = Wout / BLOCK_DIM_X;

    size_t blocks_per_out_channel = h_blocks_per_out_channel * w_blocks_per_out_channel;
    size_t num_blocks = N * Cout * blocks_per_out_channel;

    // allocate buffers
    float filter_buf[FILTER_DIM * FILTER_DIM];
    float omap_buf[BLOCK_DIM_X * BLOCK_DIM_Y];
    float* imap_buf;

    // Buffer
    CircularBuffer::FIFO<float, IMAP_DIM_X * IMAP_DIM_Y, BUFFERS> fifo(bsg_y, bsg_x-1, bsg_y, bsg_x+1);

    // ACK variable
    int ack = 0;

    // Config
    // 0 -- idle
    // 1 -- imap DMA
    // 2 -- compute
    // 3 -- polyA stoppper

    // 8 rows handle one image collectively
    // 4 columns each on 1 filter
    // image sub blocks are unrolled vertically -- each row is one sub block
    // image channels are unrolled horizentally -- each col is one channel
    /* char systolic_resnet[8][16] = { */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /* }; */

    /* char tile_config = systolic_resnet[bsg_y][bsg_x]; */

    bool should_pass = bsg_x == 15 ? false : true;

    // Job dispatch
    size_t block_y = bsg_y / 2;
    size_t block_x = bsg_y % 2;

    // this one reads the filter in forward order
    // then write it into SPM is rotated order
    auto filterDMA_rotate = [&](size_t filter_id, size_t channel_id) {
      float* filter_src_base = (float*)filter.data_ptr();
      uint32_t* filter_src_strides = filter.get_strides();
      filter_src_base += filter_id * filter_src_strides[0] + channel_id * filter_src_strides[1];
      fill_filter_buffer_rotate<FILTER_DIM>(filter_src_base, filter_buf);
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

    bool is_first_col = bsg_x == 0;

    auto compute_job = [&](size_t block_x, size_t block_y) {
      size_t channel_offset = bsg_x;

      for (size_t image_id = 0; image_id < N; image_id++) {
        for (size_t channel_id = channel_offset; channel_id < Cout; channel_id += 16) {
          if (channel_id < Cout) {

            // Reset output buffer
            reset_buffer<BLOCK_DIM_X, BLOCK_DIM_Y>(omap_buf);

            for (size_t filter_id = 0; filter_id < Cin; filter_id++) {

              if ( is_first_col ) {
                load_conv_imap( imap, block_x, block_y,
                                image_id, channel_id, filter_id,
                                N, Cout, Cin,
                                &ack,
                                fifo );
              }

              imap_buf = fifo.obtain_rd_ptr();

              if (should_pass && channel_id+1 < Cout) {
                float* imap_buf_remote = fifo.obtain_wr_ptr();
                spcpy_imap(imap_buf_remote, imap_buf);
                fifo.finish_wr_ptr();
              }

              // Load filter
              filterDMA_rotate(filter_id, channel_id);

              // do conv
              conv2d_3x3_16<BLOCK_DIM_X, BLOCK_DIM_Y, IMAP_DIM_X, IMAP_DIM_Y, FILTER_DIM>(imap_buf, filter_buf, omap_buf);

              fifo.finish_rd_ptr();
            }

            // write omap back
            omapDMA(image_id, channel_id, block_x, block_y);

          } // filter_id
        } // channel_id
      } // image_id
    }; // compute_job

    // put a sync after init ... otherwise we can deadlock
    g_barrier.sync();

    bsg_cuda_print_stat_start(8);

    compute_job(block_x, block_y);

    bsg_cuda_print_stat_end(8);

    g_barrier.sync();
    return 0;
  }



  __attribute__ ((noinline))  int tensorlib_conv_resnet_32_3x3_32x32_back_weight_smu(
    hb_tensor_t* output,
    hb_tensor_t* input,
    hb_tensor_t* weight,
    hb_vector_t* padding,
    hb_vector_t* strides) {

    HBTensor<float, 4> filter(output);
    HBTensor<float, 4> imap(input);
    HBTensor<float, 4> grad(weight);
    HBVector<uint32_t> p(padding);
    HBVector<uint32_t> s(strides);

    // extract parameters
    auto N      = filter.dim(0); // number of filters to calculate grad for
    auto Cout   = filter.dim(1); // number of channels in the images
    auto Hout   = filter.dim(2);
    auto Wout   = filter.dim(3);   // filter dimensions
    auto N_imap = imap.dim(0);   // number of images
    auto Hin    = imap.dim(2);   // image dimensions
    auto Win    = imap.dim(3);
    auto Hk     = grad.dim(2);   // grad dimensions
    auto Wk     = grad.dim(3);

    // cross check
    hb_assert(FILTER_DIM == Hout);
    hb_assert(FILTER_DIM == Wout);
    hb_assert(RAW_DIM == Hin);  // assume we are doing 32x32 -> 32x32
    hb_assert(RAW_DIM == Win);
    hb_assert(RAW_DIM == Hk);
    hb_assert(RAW_DIM == Wk);
    hb_assert(PADDING == p[0]); // assume padding == 1
    hb_assert(PADDING == p[1]);
    hb_assert(PADDING == s[0]); // assume stride == 1
    hb_assert(PADDING == s[1]);

    hb_assert(Hk % BLOCK_DIM_Y == 0); // we dont have partial blocks
    hb_assert(Wk % BLOCK_DIM_X == 0);

    // Here we break grad into blocks
    size_t h_blocks_per_out_channel = Hk / BLOCK_DIM_Y;
    size_t w_blocks_per_out_channel = Wk / BLOCK_DIM_X;

    size_t blocks_per_out_channel = h_blocks_per_out_channel * w_blocks_per_out_channel;
    size_t num_blocks = N * Cout; // parallel over filter x channel

    float filter_buf[FILTER_DIM * FILTER_DIM];      //   5x5 * 4 = 100B
    float* imap_buf;
    float* grad_buf;

    CircularBuffer::FIFO<float, IMAP_DIM_X * IMAP_DIM_Y, BUFFERS> imap_fifo(bsg_y, bsg_x-1, bsg_y, bsg_x+1);
    CircularBuffer::FIFO<float, BLOCK_DIM_X * BLOCK_DIM_Y, BUFFERS> grad_fifo(bsg_y-1, bsg_x, bsg_y+1, bsg_x);

    // Config
    // 0 -- idle
    // 1 -- imap DMA
    // 2 -- compute
    // 3 -- grad DMA

    // unroll filters channels vertically
    // unroll filters horizentally (a row)
    // grad needs to be distributed to all channels in a filter
    // input images need to be distributed to all filters
    /* char systolic_resnet[8][16] = { */
    /*   {0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /*   {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, */
    /* }; */

    /* char tile_config = systolic_resnet[bsg_y][bsg_x]; */

    bool should_pass_imap = bsg_x == (BSG_TILE_GROUP_X_DIM - 1) ? false : true;
    bool should_pass_grad = bsg_y == (BSG_TILE_GROUP_Y_DIM - 1) ? false : true;


    auto filterDMA_wb = [&](size_t filter_id, size_t channel_id) {
      bsg_attr_remote float* filter_dst_base = (float*)filter.data_ptr();
      uint32_t* filter_dst_strides = filter.get_strides();
      filter_dst_base += filter_id * filter_dst_strides[0] + channel_id * filter_dst_strides[1];
      bsg_unroll(FILTER_DIM * FILTER_DIM)
      for (size_t i = 0; i < FILTER_DIM * FILTER_DIM; i++) {
        filter_dst_base[i] = filter_buf[i];
      }
    };

    /* auto gradDMA = [&](size_t image_id, size_t channel_id, size_t block_x, size_t block_y) { */
    /*   size_t grad_x = block_x * BLOCK_DIM_X; */
    /*   size_t grad_y = block_y * BLOCK_DIM_Y; */
    /*   float* grad_src_base = (float*)grad.data_ptr(); */
    /*   uint32_t* grad_src_strides = grad.get_strides(); */
    /*   grad_src_base += image_id * grad_src_strides[0] + channel_id * grad_src_strides[1]; */
    /*   grad_src_base += grad_y * grad_src_strides[2] + grad_x * grad_src_strides[3]; */
    /*   size_t y_step = grad_src_strides[2]; */
    /*   // fill_imap_buffer<BLOCK_DIM_X, BLOCK_DIM_Y>(grad_src_base, grad_buf, y_step); */
    /*   size_t buf_offset = 0; */
    /*   for (size_t row = 0; row < BLOCK_DIM_Y; row++) { */
    /*     // unroll by BLOCK_DIM_X == 16 */
    /*     register float tmp00 = *(grad_src_base + 0); */
    /*     register float tmp01 = *(grad_src_base + 1); */
    /*     register float tmp02 = *(grad_src_base + 2); */
    /*     register float tmp03 = *(grad_src_base + 3); */
    /*     register float tmp04 = *(grad_src_base + 4); */
    /*     register float tmp05 = *(grad_src_base + 5); */
    /*     register float tmp06 = *(grad_src_base + 6); */
    /*     register float tmp07 = *(grad_src_base + 7); */
    /*     register float tmp08 = *(grad_src_base + 8); */
    /*     register float tmp09 = *(grad_src_base + 9); */
    /*     register float tmp10 = *(grad_src_base + 10); */
    /*     register float tmp11 = *(grad_src_base + 11); */
    /*     register float tmp12 = *(grad_src_base + 12); */
    /*     register float tmp13 = *(grad_src_base + 13); */
    /*     register float tmp14 = *(grad_src_base + 14); */
    /*     register float tmp15 = *(grad_src_base + 15); */
    /*     asm volatile("": : :"memory"); */
    /*     grad_buf[buf_offset + 0]  = tmp00; */
    /*     grad_buf[buf_offset + 1]  = tmp01; */
    /*     grad_buf[buf_offset + 2]  = tmp02; */
    /*     grad_buf[buf_offset + 3]  = tmp03; */
    /*     grad_buf[buf_offset + 4]  = tmp04; */
    /*     grad_buf[buf_offset + 5]  = tmp05; */
    /*     grad_buf[buf_offset + 6]  = tmp06; */
    /*     grad_buf[buf_offset + 7]  = tmp07; */
    /*     grad_buf[buf_offset + 8]  = tmp08; */
    /*     grad_buf[buf_offset + 9]  = tmp09; */
    /*     grad_buf[buf_offset + 10] = tmp10; */
    /*     grad_buf[buf_offset + 11] = tmp11; */
    /*     grad_buf[buf_offset + 12] = tmp12; */
    /*     grad_buf[buf_offset + 13] = tmp13; */
    /*     grad_buf[buf_offset + 14] = tmp14; */
    /*     grad_buf[buf_offset + 15] = tmp15; */

    /*     buf_offset += BLOCK_DIM_X; */
    /*     grad_src_base += y_step; */
    /*   } */
    /* }; */

    /* auto gradDMA_job = [&]() { */
    /*   grad_buf = grad_fifo.get_buffer(); */
    /*   size_t filter_offset = bsg_x; */

    /*   for (size_t filter_id = filter_offset; filter_id < N; filter_id += 16) { */
    /*     for (size_t channel_id = 0; channel_id < Cout; channel_id += 8) { */
    /*       if (filter_id < N) { */
    /*         for (size_t image_id = 0; image_id < N_imap; image_id++) { */
    /*           for (size_t block_y = 0; block_y < h_blocks_per_out_channel; block_y++) { */
    /*             for (size_t block_x = 0; block_x < w_blocks_per_out_channel; block_x++) { */
    /*               gradDMA(image_id, filter_id, block_x, block_y); */
    /*               //bsg_print_hexadecimal(0xCC); */
    /*               float* grad_buf_remote = grad_fifo.obtain_wr_ptr(); */
    /*               spcpy_grad(grad_buf_remote, grad_buf); */
    /*               grad_fifo.finish_wr_ptr(); */
    /*               //bsg_print_hexadecimal(0xDD); */
    /*             } */
    /*           } */
    /*         } */
    /*       } */
    /*     } */
    /*   } */
    /* }; */

    bool is_first_row = bsg_y == 0 ? true : false;
    bool is_first_col = bsg_x == 0 ? true : false;

    int ack_col = 0;
    int ack_row = 0;

    auto compute_job = [&]() {
      size_t channel_offset = bsg_y;
      size_t filter_offset = bsg_x;

      for (size_t filter_id = filter_offset; filter_id < N; filter_id += 16) {
        for (size_t channel_id = channel_offset; channel_id < Cout; channel_id += 8) {
          if (channel_id < Cout && filter_id < N) {
            reset_buffer<FILTER_DIM, FILTER_DIM>(filter_buf);
            for (size_t image_id = 0; image_id < N_imap; image_id++) {
              for (size_t block_y = 0; block_y < h_blocks_per_out_channel; block_y++) {
                for (size_t block_x = 0; block_x < w_blocks_per_out_channel; block_x++) {

                  // Gradient loaders doing gradDMA_job()

                  if ( is_first_row ) {
                    load_conv_grad_back_weight(
                        grad,
                        filter_id, channel_id, image_id, block_y, block_x,
                        N, Cout, N_imap, h_blocks_per_out_channel, w_blocks_per_out_channel,
                        &ack_row, grad_fifo
                    );
                  }

                  // Imap loaders doing imapDMA_job()

                  if ( is_first_col ) {
                    load_conv_imap_back_weight(
                        imap,
                        filter_id, channel_id, image_id, block_y, block_x,
                        N, Cout, N_imap, h_blocks_per_out_channel, w_blocks_per_out_channel,
                        &ack_col, imap_fifo
                    );
                  }

                  // Pass grad vertically

                  grad_buf = grad_fifo.obtain_rd_ptr();

                  if (should_pass_grad && channel_id+1 < Cout) {
                    float* grad_buf_remote = grad_fifo.obtain_wr_ptr();
                    spcpy_grad(grad_buf_remote, grad_buf);
                    grad_fifo.finish_wr_ptr();
                  }

                  // Pass imap horizontally

                  imap_buf = imap_fifo.obtain_rd_ptr();

                  if (should_pass_imap && filter_id+1 < N) {
                    float* imap_buf_remote = imap_fifo.obtain_wr_ptr();
                    spcpy_imap(imap_buf_remote, imap_buf);
                    imap_fifo.finish_wr_ptr();
                  }

                  // Main computation

                  for (size_t f_y = 0; f_y < FILTER_DIM; f_y++) {
                    register float psum0 = 0;
                    register float psum1 = 0;
                    register float psum2 = 0;
                    float* imap_ptr = imap_buf + f_y * IMAP_DIM_X;
                    float* grad_ptr = grad_buf;
                    float* output = filter_buf + f_y * FILTER_DIM;
                    for (size_t y = 0; y < BLOCK_DIM_Y; y++) {
                      float *imap_row = imap_ptr;
                      float *grad_row = grad_ptr;
                      for (size_t x = 0; x < BLOCK_DIM_X; x += 8) {
                        register float grad0 = grad_row[x+0];
                        register float grad1 = grad_row[x+1];
                        register float grad2 = grad_row[x+2];
                        register float grad3 = grad_row[x+3];
                        register float grad4 = grad_row[x+4];
                        register float grad5 = grad_row[x+5];
                        register float grad6 = grad_row[x+6];
                        register float grad7 = grad_row[x+7];

                        register float imap0 = imap_row[x+0];
                        register float imap1 = imap_row[x+1];
                        register float imap2 = imap_row[x+2];
                        register float imap3 = imap_row[x+3];
                        register float imap4 = imap_row[x+4];
                        register float imap5 = imap_row[x+5];
                        register float imap6 = imap_row[x+6];
                        register float imap7 = imap_row[x+7];
                        register float imap8 = imap_row[x+8];
                        register float imap9 = imap_row[x+9];

                        /* psum0 += imap0 * grad0; */
                        /* psum1 += imap1 * grad0; */
                        /* psum2 += imap2 * grad0; */

                        /* psum0 += imap1 * grad1; */
                        /* psum1 += imap2 * grad1; */
                        /* psum2 += imap3 * grad1; */

                        /* psum0 += imap2 * grad2; */
                        /* psum1 += imap3 * grad2; */
                        /* psum2 += imap4 * grad2; */

                        /* psum0 += imap3 * grad3; */
                        /* psum1 += imap4 * grad3; */
                        /* psum2 += imap5 * grad3; */

                        /* psum0 += imap4 * grad4; */
                        /* psum1 += imap5 * grad4; */
                        /* psum2 += imap6 * grad4; */

                        /* psum0 += imap5 * grad5; */
                        /* psum1 += imap6 * grad5; */
                        /* psum2 += imap7 * grad5; */

                        /* psum0 += imap6 * grad6; */
                        /* psum1 += imap7 * grad6; */
                        /* psum2 += imap8 * grad6; */

                        /* psum0 += imap7 * grad7; */
                        /* psum1 += imap8 * grad7; */
                        /* psum2 += imap9 * grad7; */

                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum0) : "f"(imap0), "f"(grad0));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum1) : "f"(imap1), "f"(grad0));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum2) : "f"(imap2), "f"(grad0));

                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum0) : "f"(imap1), "f"(grad1));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum1) : "f"(imap2), "f"(grad1));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum2) : "f"(imap3), "f"(grad1));

                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum0) : "f"(imap2), "f"(grad2));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum1) : "f"(imap3), "f"(grad2));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum2) : "f"(imap4), "f"(grad2));

                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum0) : "f"(imap3), "f"(grad3));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum1) : "f"(imap4), "f"(grad3));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum2) : "f"(imap5), "f"(grad3));

                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum0) : "f"(imap4), "f"(grad4));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum1) : "f"(imap5), "f"(grad4));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum2) : "f"(imap6), "f"(grad4));

                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum0) : "f"(imap5), "f"(grad5));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum1) : "f"(imap6), "f"(grad5));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum2) : "f"(imap7), "f"(grad5));

                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum0) : "f"(imap6), "f"(grad6));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum1) : "f"(imap7), "f"(grad6));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum2) : "f"(imap8), "f"(grad6));

                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum0) : "f"(imap7), "f"(grad7));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum1) : "f"(imap8), "f"(grad7));
                        asm volatile("fmadd.s %0, %1, %2, %0" : "+f"(psum2) : "f"(imap9), "f"(grad7));

                      } // x
                      imap_ptr += IMAP_DIM_X;
                      grad_ptr += BLOCK_DIM_X;
                    } // y
                    output[0] += psum0;
                    output[1] += psum1;
                    output[2] += psum2;
                  } // f_y
                  imap_fifo.finish_rd_ptr();
                  grad_fifo.finish_rd_ptr();
                } // block_x
              } // block_y
            } // image_id
            filterDMA_wb(filter_id, channel_id);
          }
        } // channel_id
      } // filter_id
    }; // compute_job

    // put a sync after init ... otherwise we can deadlock
    g_barrier.sync();

    bsg_cuda_print_stat_start(9);

    compute_job();

    bsg_cuda_print_stat_end(9);

    g_barrier.sync();
    return 0;
  }


  HB_EMUL_REG_KERNEL(tensorlib_conv_resnet_32_3x3_32x32_systolic, hb_tensor_t*, hb_tensor_t*, hb_tensor_t*,
                     hb_vector_t*, hb_vector_t*)

  HB_EMUL_REG_KERNEL(tensorlib_conv_resnet_32_3x3_32x32_back_input_systolic, hb_tensor_t*, hb_tensor_t*, hb_tensor_t*,
                     hb_vector_t*, hb_vector_t*)

  HB_EMUL_REG_KERNEL(tensorlib_conv_resnet_32_3x3_32x32_back_weight_systolic, hb_tensor_t*, hb_tensor_t*, hb_tensor_t*,
                     hb_vector_t*, hb_vector_t*)

}

