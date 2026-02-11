#include <cuda_runtime.h>
#include <math.h>

#define BLOCK_SIZE 8

__constant__ float COSINE_MATRIX[BLOCK_SIZE * BLOCK_SIZE];

//INVERSE DCT KERNEL
__global__ void idct_kernel(const float* d_in, unsigned char* d_out, int W, int H) {
    __shared__ float tile[BLOCK_SIZE][BLOCK_SIZE];
    __shared__ float temp[BLOCK_SIZE][BLOCK_SIZE];
    
    int block_x = blockIdx.x * BLOCK_SIZE;
    int block_y = blockIdx.y * BLOCK_SIZE;
    
    int tx = threadIdx.x;
    int ty = threadIdx.y;
    
    int x = block_x + tx;
    int y = block_y + ty;
    
    if (x >= W || y >= H) return;
    
    int idx = y * W + x;
    
    tile[ty][tx] = d_in[idx];
    __syncthreads();
    
    float sum = 0.0f;
    for (int j = 0; j < BLOCK_SIZE; j++) {
        sum += tile[ty][j] * COSINE_MATRIX[j * BLOCK_SIZE + tx];
    }
    temp[ty][tx] = sum;
    __syncthreads();
    
    sum = 0.0f;
    for (int i = 0; i < BLOCK_SIZE; i++) {
        sum += COSINE_MATRIX[i * BLOCK_SIZE + ty] * temp[i][tx];
    }
    
    float val = sum;
    if (val < 0.0f) val = 0.0f;
    if (val > 255.0f) val = 255.0f;
    
    d_out[idx] = (unsigned char)rintf(val);
}

//LOAD COSINE MATRIX
void load_cosine_matrix(const float* cosine_matrix) {
    cudaMemcpyToSymbol(COSINE_MATRIX, cosine_matrix, BLOCK_SIZE * BLOCK_SIZE * sizeof(float));
}

//INVERSE DCT KERNEL
void gpu_idct(const float* d_dct, unsigned char* d_frame_out, int width, int height, cudaStream_t stream) {
    
    dim3 block(BLOCK_SIZE, BLOCK_SIZE);
    dim3 grid((width + BLOCK_SIZE - 1) / BLOCK_SIZE,
        (height + BLOCK_SIZE - 1) / BLOCK_SIZE);
    
    idct_kernel<<<grid, block, 0, stream>>>(d_dct, d_frame_out, width, height);
}
