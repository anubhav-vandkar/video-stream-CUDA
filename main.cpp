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

#define QUANTIZATION 10

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
        
        cudaMemcpyAsync(d_frame_uint8, gray.data, width * height,
                       cudaMemcpyHostToDevice, stream);
        
        gpu_dct(d_frame_uint8, d_frame_float, d_dct, width, height, stream);
        
        gpu_quantize(d_dct, d_quantized, QUANTIZATION, width, height, stream);
        
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

    const char* video_path = "../videos/input/" + argv[1];
    const char* output_dir = "../videos/output_gpu/";
    
    system("mkdir -p output_gpu");
    
    cout << "Starting GPU video encoding..." << endl;
    cout << "Using LZ4 compression instead of RLE" << endl;
    
    encode_video_gpu(video_path, output_dir);
    
    return 0;
}
