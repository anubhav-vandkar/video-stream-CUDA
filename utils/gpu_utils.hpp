#ifndef GPU_UTILS_H
#define GPU_UTILS_H

#include <iostream>
#include <opencv2/opencv.hpp>
#include <cuda_runtime.h>
#include <lz4.h>
#include <chrono>
#include <vector>
#include <arpa/inet.h>
#include <thread>

#include "cpu_utils.hpp"
#include "udp_utils.hpp"
#include "../dct/gpu_dct.h"
#include "../dct/gpu_idct.h"
#include "../dct/gpu_quant.h"

using namespace std;

#define QUANTIZATION 25

#define FPS 60

/*
void decode_video_gpu(
    priority_queue<pair<uint32_t, vector<char>>> &recv_buffer, 
    const char* output_path, 
    int width, int height) {
    cout << "Decoding video on GPU..." << endl;

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
    
    cout << "GPU Reconstruction Complete!" << endl;
    cout << "Decoded " << files.size() << " frames in " << elapsed << " seconds" << endl;
}
*/

void encode_video_gpu(
    const char* video_path,
    int sockfd,
    sockaddr_in& client_addr
) {

    cv::VideoCapture cap(video_path);
    if (!cap.isOpened()) {
        cerr << "Failed to open video!" << endl;
        return;
    }
    
    cv::Mat frame;
    cap.read(frame);
    
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    int width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
    int height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    
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
    
    cout<< "GPU memory allocated" << endl;
    // Load DCT matrix
    float cosine_matrix[64];
    compute_cosine_matrix(cosine_matrix);
    load_cosine_matrix(cosine_matrix);

    cout << "cosine matrix loaded to GPU" << endl;
    
    short* h_quantized = new short[width * height];
    char* lz4_buffer = new char[LZ4_compressBound(width * height * sizeof(short))];

    auto start = chrono::high_resolution_clock::now();
    
    int seq = 0;

    cout << "Starting video encoding and streaming..." << endl;
    
    while (cap.read(frame)) {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        
        // GPU encode
        cudaMemcpy(d_frame_uint8, gray.data, width * height, cudaMemcpyHostToDevice);
        gpu_dct(d_frame_uint8, d_frame_float, d_dct, width, height, stream);
        gpu_quantize(d_dct, d_quantized, QUANTIZATION, width, height, stream);
        cudaStreamSynchronize(stream);
        
        cudaMemcpy(h_quantized, d_quantized, width * height * sizeof(short), cudaMemcpyDeviceToHost);

        cout << "Encoded frame " << seq << endl;
        
        // LZ4 compress
        int compressed_size = LZ4_compress_default(
            (const char*)h_quantized, lz4_buffer,
            width * height * sizeof(short),
            LZ4_compressBound(width * height * sizeof(short))
        );
        
        cout << "Sending frame " << seq << " (compressed size: " << compressed_size << " bytes)" << endl;

        // SEND
        sendFrame(sockfd, client_addr, lz4_buffer, compressed_size, seq);
        
        seq++;
        cout << "Encoded and sent frame " << seq << " (compressed size: " << compressed_size << " bytes)" << endl;
        
        // Rate limit
        usleep(16500);  // 60 FPS
    }

    sendStreamEnd(sockfd, client_addr, seq);

    cout << "Finished encoding and streaming video." << endl;
    
    auto end = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed = end - start;
    cout << "Encoding time: " << elapsed.count() << " seconds\n";
    
    cudaFree(d_frame_uint8);
    cudaFree(d_frame_float);
    cudaFree(d_dct);
    cudaFree(d_quantized);
    delete[] h_quantized;
    delete[] lz4_buffer;
    cudaStreamDestroy(stream);
    cap.release();
    
}

#endif //GPU_UTILS_H