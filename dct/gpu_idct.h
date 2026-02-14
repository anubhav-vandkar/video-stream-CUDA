#ifndef GPU_IDCT_H
#define GPU_IDCT_H

#include <cuda_runtime.h>

void load_cosine_matrix_idct(const float* cosine_matrix);

void gpu_idct(const float* d_dct, unsigned char* d_frame_out, int width, int height, cudaStream_t stream);

#endif //GPU_IDCT_H
