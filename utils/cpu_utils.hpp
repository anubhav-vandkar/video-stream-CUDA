#ifndef CPU_UTILS_H
#define CPU_UTILS_H

#include <math.h>
#include <stdint.h>

#define BLOCK_SIZE 8

void compute_cosine_matrix(float* matrix) {
    const float pi = 3.14159265358979323846f;
    const float factor = pi / (2.0f * BLOCK_SIZE);
    
    //COSINE VALUES
    for (int u = 0; u < BLOCK_SIZE; u++) {
        for (int x = 0; x < BLOCK_SIZE; x++) {
            matrix[u * BLOCK_SIZE + x] = cosf((2.0f * x + 1.0f) * u * factor);
        }
    }
    
    //ALPHA NORMALIZATION
    float alpha_0 = sqrtf(1.0f / BLOCK_SIZE);
    float alpha_n = sqrtf(2.0f / BLOCK_SIZE);
    
    for (int x = 0; x < BLOCK_SIZE; x++) {
        matrix[0 * BLOCK_SIZE + x] *= alpha_0;
    }
    
    for (int u = 1; u < BLOCK_SIZE; u++) {
        for (int x = 0; x < BLOCK_SIZE; x++) {
            matrix[u * BLOCK_SIZE + x] *= alpha_n;
        }
    }
}

void cpu_dequantize(const short* quantized, float* dct, int size, int Q) {
    for (int i = 0; i < size; i++) {
        dct[i] = (float)quantized[i] * Q;
    }
}

void idct_8x8(const float* dct_block, float* output_block, const float* cosine_matrix) {
    float temp[64];
    
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 8; k++) {
                sum += cosine_matrix[k * 8 + i] * dct_block[k * 8 + j];
            }
            temp[i * 8 + j] = sum;
        }
    }
    
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 8; k++) {
                sum += temp[i * 8 + k] * cosine_matrix[k * 8 + j];
            }
            output_block[i * 8 + j] = sum;
        }
    }
}

uint8_t* cpu_idct_frame(const short* quantized, int width, int height, int Q) {
    // Allocate buffers
    float* dct_coeffs = new float[width * height];
    uint8_t* pixels = new uint8_t[width * height];
    
    // Dequantize
    cpu_dequantize(quantized, dct_coeffs, width * height, Q);
    
    // Compute cosine matrix
    float cosine_matrix[64];
    compute_cosine_matrix(cosine_matrix);
    
    // Process each 8x8 block
    for (int by = 0; by < height; by += 8) {
        for (int bx = 0; bx < width; bx += 8) {
            float dct_block[64];
            float output_block[64];
            
            // Extract block
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    dct_block[y * 8 + x] = dct_coeffs[(by + y) * width + (bx + x)];
                }
            }
            
            // IDCT
            idct_8x8(dct_block, output_block, cosine_matrix);
            
            // Put back, add 128, clamp
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    float val = output_block[y * 8 + x] + 128.0f;
                    if (val < 0) val = 0;
                    if (val > 255) val = 255;
                    pixels[(by + y) * width + (bx + x)] = (uint8_t)val;
                }
            }
        }
    }
    
    delete[] dct_coeffs;
    return pixels;
}

#endif //CPU_UTILS_H
