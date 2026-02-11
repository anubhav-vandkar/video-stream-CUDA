#include <cuda_runtime.h>
#include <math.h>

__global__ void quantize_kernel(const float* d_in, short* d_out, float Q, int W, int H) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (x >= W || y >= H) return;
    
    int idx = y * W + x;
    float value = d_in[idx] / Q;
    d_out[idx] = (short)rintf(value);
}

__global__ void dequantize_kernel(const short* d_in, float* d_out, int Q, int W, int H) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (x >= W || y >= H) return;
    
    int idx = y * W + x;
    d_out[idx] = (float)d_in[idx] * Q;
}

void gpu_quantize(const float* d_dct, short* d_quantized, float Q, int width, int height, cudaStream_t stream) {
    dim3 block(16, 16);
    dim3 grid((width + 15) / 16, (height + 15) / 16);
    
    quantize_kernel<<<grid, block, 0, stream>>>(d_dct, d_quantized, Q, width, height);
}

void gpu_dequantize(const short* d_quantized, float* d_dct, int Q, int width, int height, cudaStream_t stream) {
    dim3 block(16, 16);
    dim3 grid((width + 15) / 16, (height + 15) / 16);
    
    dequantize_kernel<<<grid, block, 0, stream>>>(d_quantized, d_dct, Q, width, height);
}
