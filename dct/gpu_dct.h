#ifndef GPU_DCT_H
#define GPU_DCT_H

#include <cuda_runtime.h>

void load_cosine_matrix(const float* cosine_matrix);

void gpu_dct(unsigned char* d_frame_uint8, float* d_frame_float, float* d_dct_out, int width, int height, cudaStream_t stream);

#endif // GPU_DCT_H
