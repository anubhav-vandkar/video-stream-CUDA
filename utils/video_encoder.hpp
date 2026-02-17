#ifndef VIDEO_ENCODER_HPP
#define VIDEO_ENCODER_HPP

#include <opencv2/opencv.hpp>
#include <cuda_runtime.h>

class VideoEncoder {
private:
    cv::VideoCapture cap;
    cudaStream_t stream;
    unsigned char* d_frame_uint8;
    float* d_frame_float;
    float* d_dct;
    short* d_quantized;
    short* h_quantized;
    char* lz4_buffer;
    int width, height;
    uint32_t frame_count;
    
public:
    VideoEncoder(const char* video_path);
    ~VideoEncoder();
    
    int getWidth() { return width; }
    int getHeight() { return height; }
    
    struct EncodedFrame {
        char* data;
        uint32_t size;
        uint32_t seq;
    };
    
    EncodedFrame* encodeNextFrame();
};

#endif // VIDEO_ENCODER_HPP