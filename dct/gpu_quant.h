#ifndef GPU_QUANT_H
#define GPU_QUANT_H

#include <cuda_runtime.h>

void gpu_quantize(const float* d_dct, short* d_quantized, float Q, int width, int height, cudaStream_t stream);

void gpu_dequantize(const short* d_quantized, float* d_dct, int Q, int width, int height, cudaStream_t stream);

#endif // GPU_QUANT_H
