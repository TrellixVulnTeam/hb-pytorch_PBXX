//====================================================================
// addmm kernel common subroutine
// 06/20/2020 Lin Cheng (lc873@cornell.edu)
//====================================================================

inline void compute_simple(
          float* dest,
          float* sp_mat1,
          float* sp_mat2,
          int dim_y,
          int dim_x,
          int mid_dim) {
    for (int i = 0; i < dim_y; i++) {
        int dest_row_offset = i * dim_x;
        int mat1_row_offset = i * mid_dim;
        for (int j = 0; j < dim_x; j++) {
            int k = 0;
            for (;k < mid_dim - 8; k += 8) {
                int mat1_idx = mat1_row_offset + k;
                int mat2_idx = k * dim_x + j;
                register float tmp0 = sp_mat1[mat1_idx] * sp_mat2[mat2_idx];
                register float tmp1 = sp_mat1[mat1_idx + 1] * sp_mat2[mat2_idx + dim_x];
                register float tmp2 = sp_mat1[mat1_idx + 2] * sp_mat2[mat2_idx + 2 * dim_x];
                register float tmp3 = sp_mat1[mat1_idx + 3] * sp_mat2[mat2_idx + 3 * dim_x];
                register float tmp4 = sp_mat1[mat1_idx + 4] * sp_mat2[mat2_idx + 4 * dim_x];
                register float tmp5 = sp_mat1[mat1_idx + 5] * sp_mat2[mat2_idx + 5 * dim_x];
                register float tmp6 = sp_mat1[mat1_idx + 6] * sp_mat2[mat2_idx + 6 * dim_x];
                register float tmp7 = sp_mat1[mat1_idx + 7] * sp_mat2[mat2_idx + 7 * dim_x];
                dest[dest_row_offset + j] += (tmp0 + tmp1 + tmp2 + tmp3 + tmp4 + tmp5 + tmp6 + tmp7);
            }
            // fixup
            register float tmp_fix = 0.0f;
            for (;k < mid_dim; k++) {
                int mat1_idx = mat1_row_offset + k;
                int mat2_idx = k * dim_x + j;
                tmp_fix += sp_mat1[mat1_idx] * sp_mat2[mat2_idx];
            }
            dest[dest_row_offset + j] += tmp_fix;
        }
    }
}
