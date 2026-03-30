#ifndef ENCODER_INTERFACE_HPP
#define ENCODER_INTERFACE_HPP

#include <opencv2/opencv.hpp>
#include <string>
#include <arpa/inet.h>
#include <chrono>
#include <iostream>

#include "../config.hpp"
#include "cpu_utils.hpp"
#include "udp_utils.hpp"

// Only include GPU headers if GPU compilation enabled
#ifdef USE_GPU
#include <cuda_runtime.h>
#include "gpu_utils.hpp"
#endif

void encode_and_send(
    const char* video_path,
    int sockfd,
    sockaddr_in& client_addr,
    bool use_gpu = false
) {
    cv::VideoCapture cap(video_path);
    if (!cap.isOpened()) {
        std::cerr << "Failed to open video: " << video_path << "\n";
        return;
    }
    
    int width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
    int height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    
    std::cout << "Video: " << width << "x" << height << "\n";
    
#ifdef USE_GPU
    if (use_gpu) {
        std::cout << "Using GPU encoder\n";
        encode_video_gpu_and_send(video_path, sockfd, client_addr);
        return;
    }
#else
    if (use_gpu) {
        std::cout << "GPU requested but not available (compiled without CUDA)\n";
        std::cout << "Falling back to CPU encoder\n";
    }
#endif
    
    // CPU encoder (always available)
    std::cout << "Using CPU encoder (AVX2)\n";
    
    cv::Mat frame, gray;
    uint32_t seq = 0;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    while (cap.read(frame)) {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        
        EncodedFrame encoded = cpu_encode_frame(gray, seq);
        
        sendFrame(sockfd, client_addr, (char*)encoded.data.data(), encoded.size, seq);
        
        seq++;
        
        if (seq % 100 == 0) {
            std::cout << "Encoded " << seq << " frames (CPU)\n";
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    
    sendStreamEnd(sockfd, client_addr, seq);
    
    std::cout << "CPU Encoding complete!\n";
    std::cout << "Encoded " << seq << " frames in " << elapsed << " seconds\n";
    std::cout << "FPS: " << (seq / elapsed) << "\n";
    
    cap.release();
}

#endif