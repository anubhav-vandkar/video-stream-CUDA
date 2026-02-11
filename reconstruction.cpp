#include <iostream>
#include <opencv2/opencv.hpp>
#include <cuda_runtime.h>
#include <lz4.h>
#include <dirent.h>
#include <algorithm>
#include <vector>
#include <chrono>
#include "cpu_utils.h"
#include "dct/gpu_quant.h"
#include "dct/gpu_idct.h"

using namespace std;

#define QUANTIZATION 10
#define FPS 30

short* load_compressed_frame(const char* filename, int width, int height) {
    FILE* f = fopen(filename, "rb");
    if (!f) return nullptr;
    
    fseek(f, 0, SEEK_END);
    int compressed_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* compressed = new char[compressed_size];
    fread(compressed, 1, compressed_size, f);
    fclose(f);
    
    short* decompressed = new short[width * height];
    int result = LZ4_decompress_safe(
        compressed,
        (char*)decompressed,
        compressed_size,
        width * height * sizeof(short)
    );
    
    delete[] compressed;
    
    if (result < 0) {
        delete[] decompressed;
        return nullptr;
    }
    
    return decompressed;
}

vector<string> get_frame_files(const char* dir) {
    vector<string> files;
    DIR* d = opendir(dir);
    if (!d) return files;
    
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        string name = entry->d_name;
        if (name.find(".lz4") != string::npos) {
            files.push_back(string(dir) + "/" + name);
        }
    }
    closedir(d);
    
    sort(files.begin(), files.end());
    return files;
}

//GPU RECON
void reconstruct_video_gpu(const char* input_dir, const char* output_path,
                          int width, int height) {
    
    vector<string> files = get_frame_files(input_dir);
    if (files.empty()) {
        cerr << "No frames found!" << endl;
        return;
    }
    
    cout << "Found " << files.size() << " frames to decode" << endl;
    
    cv::VideoWriter writer(output_path, 
                          cv::VideoWriter::fourcc('m','p','4','v'),
                          FPS, cv::Size(width, height), false);
    
    // Create CUDA stream
    cudaStream_t stream;
    cudaStreamCreate(&stream);
    
    short* d_quantized;
    float* d_dct;
    unsigned char* d_reconstructed;
    
    cudaMalloc(&d_quantized, width * height * sizeof(short));
    cudaMalloc(&d_dct, width * height * sizeof(float));
    cudaMalloc(&d_reconstructed, width * height);
    
    // Load DCT matrix
    float cosine_matrix[64];
    compute_cosine_matrix(cosine_matrix);
    load_cosine_matrix_idct(cosine_matrix);
    
    unsigned char* h_reconstructed = new unsigned char[width * height];
    
    auto start = chrono::high_resolution_clock::now();
    
    for (int i = 0; i < files.size(); i++) {
        short* h_quantized = load_compressed_frame(files[i].c_str(), width, height);
        if (!h_quantized) {
            cerr << "Failed to load frame " << i << endl;
            continue;
        }
        cudaMemcpyAsync(d_quantized, h_quantized, width * height * sizeof(short),
                       cudaMemcpyHostToDevice, stream);
        
        gpu_dequantize(d_quantized, d_dct, QUANTIZATION, width, height, stream);
        
        gpu_idct(d_dct, d_reconstructed, width, height, stream);
        
        cudaMemcpyAsync(h_reconstructed, d_reconstructed, width * height,
                       cudaMemcpyDeviceToHost, stream);
        
        cudaStreamSynchronize(stream);
        
        cv::Mat frame(height, width, CV_8UC1, h_reconstructed);
        writer.write(frame);
        
        delete[] h_quantized;
        
        if ((i + 1) % 100 == 0) {
            cout << "Decoded " << (i + 1) << " frames..." << endl;
        }
    }
    
    auto end = chrono::high_resolution_clock::now();
    double elapsed = chrono::duration<double>(end - start).count();
    
    cudaFree(d_quantized);
    cudaFree(d_dct);
    cudaFree(d_reconstructed);
    cudaStreamDestroy(stream);
    delete[] h_reconstructed;
    writer.release();
    
    cout << "\nGPU Reconstruction Complete!" << endl;
    cout << "Decoded " << files.size() << " frames in " << elapsed << " seconds" << endl;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        cout << "Usage: " << argv[0] << " <input_dir> <output_video> <width> <height>" << endl;
        return 1;
    }
    
    const char* input_dir = argv[1];
    const char* output_path = argv[2];
    int width = atoi(argv[3]);
    int height = atoi(argv[4]);
    
    cout << "Starting GPU video reconstruction..." << endl;
    reconstruct_video_gpu(input_dir, output_path, width, height);
    
    return 0;
}
