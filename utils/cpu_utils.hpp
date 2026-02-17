#ifndef CPU_UTILS_H
#define CPU_UTILS_H

#include <math.h>
#include <stdint.h>
#include <immintrin.h>

//Everything hardcoded to 8 due to AVX 

void compute_cosine_matrix(float* matrix) {
    const float pi = 3.14159265358979323846f;
    const float factor = pi / (2.0f * 8);
    
    //COSINE VALUES
    for (int u = 0; u < 8; u++) {
        for (int x = 0; x < 8; x++) {
            matrix[u * 8 + x] = cosf((2.0f * x + 1.0f) * u * factor);
        }
    }
    
    //ALPHA NORMALIZATION
    float alpha_0 = sqrtf(1.0f / 8);
    float alpha_n = sqrtf(2.0f / 8);
    
    for (int x = 0; x < 8; x++) {
        matrix[0 * 8 + x] *= alpha_0;
    }
    
    for (int u = 1; u < 8; u++) {
        for (int x = 0; x < 8; x++) {
            matrix[u * 8 + x] *= alpha_n;
        }
    }
}

void cpu_dequantize(const short* quantized, float* dct, int size, int Q) {
    __m256 vQ = _mm256_set1_ps((float)Q);  // Broadcast Q to 8 floats
    
    int i = 0;
    for (; i <= size - 8; i += 8) {
        __m128i vi16 = _mm_loadu_si128((__m128i*)&quantized[i]);
        
        __m256i vi32 = _mm256_cvtepi16_epi32(vi16);
        __m256 vf = _mm256_cvtepi32_ps(vi32);
        
        // Multiply by Q
        vf = _mm256_mul_ps(vf, vQ);
        
        // Store 8 floats
        _mm256_storeu_ps(&dct[i], vf);
    }
    
    for (; i < size; i++) {
        dct[i] = (float)quantized[i] * Q;
    }
}

void idct_8x8(const float* dct_block, float* output_block, const float* cosine_matrix) {
    float temp[64];
    
    for (int i = 0; i < 8; i++) {
        __m256 vsum = _mm256_setzero_ps();

        for (int j = 0; j < 8; j++) {
            __m256 vc = _mm256_set1_ps(cosine_matrix[j * 8 + i]);
            
            __m256 vb = _mm256_loadu_ps(&dct_block[j * 8]);
            
            vsum = _mm256_fmadd_ps(vc, vb, vsum);
        }
        _mm256_storeu_ps(&temp[i * 8], vsum);
    }
    

    for (int i = 0; i < 8; i++) {
        __m256 vsum = _mm256_setzero_ps();

        for (int j = 0; j < 8; j++) {
            __m256 vt = _mm256_set1_ps(temp[i * 8 + j]);
            
            __m256 vc = _mm256_loadu_ps(&cosine_matrix[j * 8]);
            
            vsum = _mm256_fmadd_ps(vt, vc, vsum);
        } 
        _mm256_storeu_ps(&output_block[i * 8], vsum);
    }
}

void store_block(const float* output_block, uint8_t* pixels, int bx, int by, int width) {
    
    __m256 v128 = _mm256_set1_ps(128.0f);
    __m256 vmin = _mm256_set1_ps(0.0f);
    __m256 vmax = _mm256_set1_ps(255.0f);
    
    for (int y = 0; y < 8; y++) {
        // Load 8 output values
        __m256 vout = _mm256_loadu_ps(&output_block[y * 8]);
        
        vout = _mm256_add_ps(vout, v128);
        
        // Clamp to min max values
        vout = _mm256_max_ps(vout, vmin);
        vout = _mm256_min_ps(vout, vmax);
        
        // Convert float
        __m256i vi32 = _mm256_cvtps_epi32(vout);
        
        __m128i lo = _mm256_extracti128_si256(vi32, 0);
        __m128i hi = _mm256_extracti128_si256(vi32, 1);
        __m128i vi16 = _mm_packs_epi32(lo, hi);
        
        __m128i vi8 = _mm_packus_epi16(vi16, _mm_setzero_si128());
        
        // Store 8 bytes to output
        _mm_storel_epi64((__m128i*)&pixels[(by + y) * width + bx], vi8);
    }
}

uint8_t* cpu_idct_frame(const short* quantized, int width, int height, int Q) {
    // Allocate buffers
    float* dct_coeffs = new float[width * height];
    uint8_t* pixels = new uint8_t[width * height];
    
    // Dequantize
    cpu_dequantize(quantized, dct_coeffs, width * height, Q);
    
    // Compute static cosine matrix
    static float cosine_matrix[64] = {0};
    static bool initialized = false;
    
    if (!initialized) {
        compute_cosine_matrix(cosine_matrix);
        initialized = true;
    }
    
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
            
            // Store pixels
            store_block(output_block, pixels, bx, by, width);
        }
    }
    
    delete[] dct_coeffs;
    return pixels;
}

#endif //CPU_UTILS_H
