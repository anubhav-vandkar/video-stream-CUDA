#ifndef DECODER_THREAD_HPP
#define DECODER_THREAD_HPP

#include <iostream>
#include <opencv2/opencv.hpp>
#include <lz4.h>
#include "recv_thread.hpp"
#include "cpu_utils.hpp"
#include "../config.hpp"

using namespace std;

class DecoderThread {
private:
    FrameQueue& frame_queue;
    FILE* ffplay;
    cv::VideoWriter& writer;
    int width;
    int height;
    
    uint32_t frames_decoded = 0;

public:
    DecoderThread(FrameQueue& queue, FILE* ffplay, 
                  cv::VideoWriter& writer, int width, int height, int Q)
        : frame_queue(queue), ffplay(ffplay), writer(writer),
          width(width), height(height) {}
    
    void run() {
        cout << "Decoder thread started\n";
        
        while (true) {
            CompressedFrame frame;
            
            // Get frame from queue (blocks until available)
            if (!frame_queue.pop(frame)) {
                break;
            }
            
            // Decompress
            short* quantized = new short[width * height];
            
            int result = LZ4_decompress_safe(
                (char*)frame.data.data(),
                (char*)quantized,
                frame.data.size(),
                width * height * sizeof(short)
            );
            
            if (result < 0) {
                cerr << "LZ4 decompress failed for frame " << frame.seq << "\n";
                delete[] quantized;
                continue;
            }
            
            // CPU IDCT
            uint8_t* pixels = cpu_idct_frame(quantized, width, height);
            
            // Live playback
            if (ffplay) {
                fwrite(pixels, 1, width * height, ffplay);
                fflush(ffplay);
            }
            
            // Save to file
            cv::Mat mat(height, width, CV_8UC1, pixels);
            writer.write(mat);
            
            // Cleanup
            delete[] quantized;
            delete[] pixels;
            
            frames_decoded++;
            if (frames_decoded % 100 == 0) {
                cout << "Decoded " << frames_decoded << " frames\n";
            }
        }
        
        cout << "Decoder thread done. Total: " << frames_decoded << " frames\n";
    }
};

#endif // DECODER_THREAD_HPP