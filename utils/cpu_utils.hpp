#ifndef CPU_UTILS_H
#define CPU_UTILS_H

#include <math.h>
#include <stdint.h>
#include <immintrin.h>
#include <vector>
#include <lz4.h>

#include "../config.hpp"

//Everything hardcoded to 8 due to AVX 256

struct CosineMatrix {
    float data[64] = {};
    bool  ready    = false;

    void init() {
        if (ready) return;

        const float pi     = 3.14159265358979323846f;
        const float factor = pi / (2.0f * 8);

        // Fill cosine values
        for (int u = 0; u < 8; u++) {
            for (int x = 0; x < 8; x++) {
                data[u * 8 + x] = cosf((2.0f * x + 1.0f) * u * factor);
            }
        }

        // Apply alpha normalization per row
        const float alpha_0 = sqrtf(1.0f / 8);
        const float alpha_n = sqrtf(2.0f / 8);

        for (int x = 0; x < 8; x++)
            data[0 * 8 + x] *= alpha_0;

        for (int u = 1; u < 8; u++)
            for (int x = 0; x < 8; x++)
                data[u * 8 + x] *= alpha_n;

        ready = true;
    }
};

static CosineMatrix g_cosine_matrix;

void cpu_dequantize(const short* quantized, float* dct, int size) {
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

void cpu_idct(const float* dct_block, float* output_block, const float* cosine_matrix) {
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

uint8_t* cpu_idct_frame(const short* quantized, int width, int height) {
    std::vector<float> dct_coeffs(width * height);
    uint8_t* pixels = new uint8_t[width * height];

    // dequantize entire frame
    cpu_dequantize(quantized, dct_coeffs.data(), width * height);

    // cosine matrix init
    g_cosine_matrix.init();

    // process each block
    for (int by = 0; by < height; by += 8) {
        for (int bx = 0; bx < width; bx += 8) {
            float dct_block[64];
            float output_block[64];

            // Extract the 8x8 block from the flat dct_coeffs array
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    dct_block[y * 8 + x] = dct_coeffs[(by + y) * width + (bx + x)];

            cpu_idct(dct_block, output_block, g_cosine_matrix.data);
            store_block(output_block, pixels, bx, by, width);
        }
    }

    return pixels;
}

void cpu_quantize_avx(const float* dct_coeffs, short* quantized, int size) {
    __m256 vQ = _mm256_set1_ps((float)Q);
    
    int i = 0;
    for (; i <= size - 8; i += 8) {
        // Load 8 DCT coefficients
        __m256 vdct = _mm256_loadu_ps(&dct_coeffs[i]);
        
        // Divide by Q
        vdct = _mm256_div_ps(vdct, vQ);
        
        // Round to nearest
        __m256i vi32 = _mm256_cvtps_epi32(vdct);
        
        // Convert int32 → int16
        __m128i lo = _mm256_extracti128_si256(vi32, 0);
        __m128i hi = _mm256_extracti128_si256(vi32, 1);
        __m128i vi16 = _mm_packs_epi32(lo, hi);

        // Store 8 int16 values
        _mm_storeu_si128((__m128i*)&quantized[i], vi16);
    }
    
    // Handle remainder
    for (; i < size; i++) {
        quantized[i] = (short)(rintf(dct_coeffs[i] / Q));
    }
}

// 8x8 DCT block with AVX2
void dct_8x8_avx(const float* input_block, float* output_block, const float* cosine_matrix) {

    float temp[64];
    
    // First pass: temp = C * input
    for (int u = 0; u < 8; u++) {
        __m256 vsum = _mm256_setzero_ps();
        
        for (int x = 0; x < 8; x++) {
            __m256 vc = _mm256_set1_ps(cosine_matrix[u * 8 + x]);
            __m256 vin = _mm256_loadu_ps(&input_block[x * 8]);
            vsum = _mm256_fmadd_ps(vc, vin, vsum);
        }
        
        _mm256_storeu_ps(&temp[u * 8], vsum);
    }
    
    // Second pass: output = temp * C^T
    for (int u = 0; u < 8; u++) {
        __m256 vsum = _mm256_setzero_ps();
        
        for (int v = 0; v < 8; v++) {
            __m256 vt = _mm256_set1_ps(temp[u * 8 + v]);
            __m256 vc = _mm256_loadu_ps(&cosine_matrix[v * 8]);
            vsum = _mm256_fmadd_ps(vt, vc, vsum);
        }
        
        _mm256_storeu_ps(&output_block[u * 8], vsum);
    }
}

// Encode frame result structure
struct EncodedFrame {
    std::vector<uint8_t> data;
    uint32_t size;
    uint32_t seq;
};

// CPU encode single frame
EncodedFrame cpu_encode_frame(const cv::Mat& gray_frame, uint32_t seq) {
    int width = gray_frame.cols;
    int height = gray_frame.rows;
    
    const uint8_t* pixels = gray_frame.data;

    // Allocate buffers
    float* frame_float = new float[width * height];
    float* dct_coeffs = new float[width * height];
    short* quantized = new short[width * height];
    
    // Convert uint8 to float, subtract 128
    for (int i = 0; i < width * height; i++) {
        frame_float[i] = (float)pixels[i] - 128.0f;
    }
    
    // Compute cosine matrix once (static)
    static bool initialized = false;

    if (!initialized) {
        g_cosine_matrix.init();
        initialized = true;
    }
    
    // Process each 8x8 block
    for (int by = 0; by < height; by += 8) {
        for (int bx = 0; bx < width; bx += 8) {
            float input_block[64];
            float output_block[64];
            
            // Extract block
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    input_block[y * 8 + x] = frame_float[(by + y) * width + (bx + x)];
                }
            }
            
            // DCT
            dct_8x8_avx(input_block, output_block, g_cosine_matrix.data);
            
            // Put back
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    dct_coeffs[(by + y) * width + (bx + x)] = output_block[y * 8 + x];
                }
            }
        }
    }

    // Quantize
    cpu_quantize_avx(dct_coeffs, quantized, width * height);
    
    // LZ4 compress
    int max_compressed = LZ4_compressBound(width * height * sizeof(short));
    char* lz4_buffer = new char[max_compressed];
    
    int compressed_size = LZ4_compress_default(
        (const char*)quantized,
        lz4_buffer,
        width * height * sizeof(short),
        max_compressed
    );
    
    // Create result
    EncodedFrame result;
    result.seq = seq;
    result.data = std::vector<uint8_t>(lz4_buffer, lz4_buffer + compressed_size);
    result.size = compressed_size;
    
    delete[] frame_float;
    delete[] dct_coeffs;
    delete[] quantized;
    delete[] lz4_buffer;
    
    return result;
}

#endif //CPU_UTILS_H
