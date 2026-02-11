#ifndef CPU_UTILS_H
#define CPU_UTILS_H

#include <cmath>
#include <cstdint>

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

#endif //CPU_UTILS_H
