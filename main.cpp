#include <iostream>
#include <opencv2/opencv.hpp>
#include <cuda_runtime.h>
#include <lz4.h>
#include <chrono>
#include <vector>
#include "dct/cpu_utils.h"
#include "dct/gpu_dct.h"
#include "dct/gpu_quant.h"

using namespace std;

#define QUANTIZATION 8

void save_compressed_frame(const char* filename, const char* data, int size) {
    FILE* f = fopen(filename, "wb");
    fwrite(data, 1, size, f);
    fclose(f);
}

void encode_video_gpu(const char* video_path, const char* output_dir) {
    cv::VideoCapture cap(video_path);
    if (!cap.isOpened()) {
        cerr << "Failed to open video!" << endl;
        return;
    }
    
    cv::Mat frame;
    cap.read(frame);
    
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    int height = gray.rows;
    int width = gray.cols;
    
    cout << "Video size: " << width << "x" << height << endl;
    
    cudaStream_t stream;
    cudaStreamCreate(&stream);
    
    unsigned char* d_frame_uint8;
    float* d_frame_float;
    float* d_dct;
    short* d_quantized;
    
    cudaMalloc(&d_frame_uint8, width * height);
    cudaMalloc(&d_frame_float, width * height * sizeof(float));
    cudaMalloc(&d_dct, width * height * sizeof(float));
    cudaMalloc(&d_quantized, width * height * sizeof(short));
    
    float cosine_matrix[64];
    compute_cosine_matrix(cosine_matrix);
    load_cosine_matrix(cosine_matrix);
    
    short* h_quantized = new short[width * height];
    char* lz4_buffer = new char[LZ4_compressBound(width * height * sizeof(short))];
    
    int frame_count = 0;
    auto start = chrono::high_resolution_clock::now();
    
    cap.set(cv::CAP_PROP_POS_FRAMES, 0);
    
    while (cap.read(frame)) {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        
        cudaMemcpyAsync(d_frame_uint8, gray.data, width * height, cudaMemcpyHostToDevice, stream);
        if (frame_count == 0) {
            // Check what's being sent to GPU
            cout << "Grayscale frame first 20 pixels:\n";
            for (int i = 0; i < 20; i++) {
                cout << (int)gray.data[i] << " ";
            }
            cout << "\n";
            
            // After copying to GPU and converting to float
            // Copy d_frame_float back to check
            float* h_check = new float[width * height];
            
            // After uint8_to_float kernel, before DCT:
            cudaMemcpy(h_check, d_frame_uint8, width * height * sizeof(float),
                    cudaMemcpyDeviceToHost);
            
            cout << "After uint8_to_float, first 20 values:\n";
            for (int i = 0; i < 20; i++) {
                cout << h_check[i] << " ";
            }
            cout << "\n";
            
            delete[] h_check;
        }
        
        gpu_dct(d_frame_uint8, d_frame_float, d_dct, width, height, stream);
        cudaStreamSynchronize(stream);

        cudaError_t err = cudaGetLastError();
        if (err != cudaSuccess) {
            cout << "CUDA error: " << cudaGetErrorString(err) << "\n";
        }

        if (frame_count == 0) {
            // Copy DCT coefficients back to CPU
            float* h_dct = new float[width * height];
            cudaMemcpy(h_dct, d_dct, width * height * sizeof(float), 
                    cudaMemcpyDeviceToHost);
            
            cout << "After DCT, first 20 coefficients:\n";
            for (int i = 0; i < 20; i++) {
                cout << h_dct[i] << " ";
            }
            cout << "\n";
            
            // Check range
            float min_val = 1e9, max_val = -1e9;
            for (int i = 0; i < width * height; i++) {
                if (h_dct[i] < min_val) min_val = h_dct[i];
                if (h_dct[i] > max_val) max_val = h_dct[i];
            }
            cout << "DCT range: [" << min_val << ", " << max_val << "]\n";
            
            delete[] h_dct;
        }
                
        gpu_quantize(d_dct, d_quantized, QUANTIZATION, width, height, stream);
        cudaStreamSynchronize(stream);
        if (frame_count == 0) {
            cout << "After GPU quantize, first 20 values:\n";
            for (int i = 0; i < 20; i++) {
                cout << h_quantized[i] << " ";
            }
            cout << "\n";
            
            // Check range
            int16_t min_val = 1000, max_val = -1000;
            for (int i = 0; i < width * height; i++) {
                if (h_quantized[i] < min_val) min_val = h_quantized[i];
                if (h_quantized[i] > max_val) max_val = h_quantized[i];
            }
            cout << "Quantized range: [" << min_val << ", " << max_val << "]\n";
        }
        
        cudaMemcpyAsync(h_quantized, d_quantized, width * height * sizeof(short),
                       cudaMemcpyDeviceToHost, stream);
        
        cudaStreamSynchronize(stream);

        int compressed_size = LZ4_compress_default(
            (const char*)h_quantized,
            lz4_buffer,
            width * height * sizeof(short),
            LZ4_compressBound(width * height * sizeof(short))
        );
        
        char filename[256];
        sprintf(filename, "%s/frame_%05d.lz4", output_dir, frame_count);
        save_compressed_frame(filename, lz4_buffer, compressed_size);
        
        frame_count++;
        
        if (frame_count % 100 == 0) {
            cout << "Processed " << frame_count << " frames..." << endl;
        }
    }
    
    auto end = chrono::high_resolution_clock::now();
    double elapsed = chrono::duration<double>(end - start).count();
    
    cudaFree(d_frame_uint8);
    cudaFree(d_frame_float);
    cudaFree(d_dct);
    cudaFree(d_quantized);
    cudaStreamDestroy(stream);
    delete[] h_quantized;
    delete[] lz4_buffer;
    cap.release();
    
    cout << "GPU Encoding Complete!" << endl;
    cout << "Processed " << frame_count << " frames in " << elapsed << " seconds" << endl;
    cout << "Speedup calculation needs CPU baseline for comparison" << endl;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        cout << "Usage: " << argv[0] << " <file_name_with_extension>" << endl;
        return 1;
    }

    string video_path = string("../files/input/") + argv[1];
    const char* output_dir = "../files/output_gpu/";
    
    system("mkdir -p ../files/output_gpu");
    
    cout << "Starting GPU video encoding..." << endl;
    cout << "Using LZ4 compression instead of RLE" << endl;
    
    encode_video_gpu(video_path.c_str(), output_dir);
    
    return 0;
}
