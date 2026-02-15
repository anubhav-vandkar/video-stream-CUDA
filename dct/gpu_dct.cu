#include <cuda_runtime.h>
#include <stdio.h>

#define BLOCK_SIZE 8

__constant__ float COSINE_MATRIX[BLOCK_SIZE * BLOCK_SIZE];

//INT TO FLOAT KERNEL
__global__ void int_to_float_kernel(const unsigned char* d_in, float* d_out, int W, int H) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = W * H;
    
    if (idx < total) {
        d_out[idx] = (float)d_in[idx] - 128.0f;
    }
}

//GPU DCT KERNEL
__global__ void dct_kernel(const float* d_in, float* d_out, int W, int H) {
    __shared__ float tile[BLOCK_SIZE][BLOCK_SIZE];
    __shared__ float temp[BLOCK_SIZE][BLOCK_SIZE];
    
    int block_x = blockIdx.x * BLOCK_SIZE;
    int block_y = blockIdx.y * BLOCK_SIZE;
    
    int tx = threadIdx.x;
    int ty = threadIdx.y;
    
    int x = block_x + tx;
    int y = block_y + ty;
    
    if (x >= W || y >= H) return;
    
    tile[ty][tx] = d_in[y * W + x];
    __syncthreads();
    
    float sum = 0.0f;
    for (int i = 0; i < BLOCK_SIZE; i++) {
        sum += tile[ty][i] * COSINE_MATRIX[i * BLOCK_SIZE + tx];
    }
    temp[ty][tx] = sum;
    __syncthreads();
    
    sum = 0.0f;
    for (int i = 0; i < BLOCK_SIZE; i++) {
        sum += COSINE_MATRIX[i * BLOCK_SIZE + ty] * temp[i][tx];
    }
    
    d_out[y * W + x] = sum;
}

//LOAD COSINE MATRIX
void load_cosine_matrix(const float* cosine_matrix) {
    cudaMemcpyToSymbol(COSINE_MATRIX, cosine_matrix, BLOCK_SIZE * BLOCK_SIZE * sizeof(float));
}

//GPU DCT
void gpu_dct(unsigned char* d_frame_uint8, float* d_frame_float, float* d_dct_out, int width, int height, cudaStream_t stream) {
    
    int total = width * height;
    dim3 block_1d(256);
    dim3 grid_1d((total + 255) / 256);
    
    int_to_float_kernel<<<grid_1d, block_1d, 0, stream>>>(
        d_frame_uint8, d_frame_float, width, height
    );
    
    dim3 block_2d(BLOCK_SIZE, BLOCK_SIZE);
    dim3 grid_2d((width + BLOCK_SIZE - 1) / BLOCK_SIZE,
                 (height + BLOCK_SIZE - 1) / BLOCK_SIZE);
    
    dct_kernel<<<grid_2d, block_2d, 0, stream>>>(
        d_frame_float, d_dct_out, width, height
    );
}
