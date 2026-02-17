#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <fstream>
#include <vector>
#include <map>
#include <opencv2/opencv.hpp>
#include <lz4.h>
#include <libavformat/avformat.h>

#include "TCP_utils.hpp"
#include "udp_utils.hpp"
#include "cpu_utils.hpp"

using namespace std;

#define Q 50

int main(int argc, char* argv[]){
    if (argc != 6) {
        cerr << "Usage: ./client <dest-ip> <remote-filename> <output-filename> <width> <height>\n";
        return 1;
    }

    const char *dest_ip = argv[1];
    const char *remote_filename = argv[2];
    const char *output_filename = argv[3];

    const int width = atoi(argv[4]);
    const int height = atoi(argv[5]);

    using clock = chrono::steady_clock;

    int client_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_fd < 0) {
        cerr << "Socket creation failed\n";
        return 1;
    }

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(9000);
    inet_pton(AF_INET, dest_ip, &dest.sin_addr);

    char buf[150000];
    socklen_t dlen = sizeof(dest);

    sendRequest(client_fd, dest, remote_filename);

    FILE* ffplay = popen(
        "ffplay -f rawvideo -pixel_format gray -video_size 1920x1080 "
        "-framerate 30 -i - 2>/dev/null",
        "w"
    );

    if (!ffplay) {
        cerr << "Warning: ffplay not available. Saving to file only.\n";
    }

    // Open video writer for saving
    cv::VideoWriter writer(
        output_filename,
        cv::VideoWriter::fourcc('m','p','4','v'),
        30,
        cv::Size(width, height),
        false
    );

    cout << "Receiving and decoding video...\n";
    if (ffplay) cout << "Live playback started in ffplay window\n";

    cout<< "Waiting for video stream...\n";
    ofstream outfile(output_filename, ios::binary);

    uint32_t next_to_decode = 0;
    uint32_t frames_received = 0;

    map<uint32_t, vector<uint8_t>> recv_buffer;

    while (true) {
        int n = recvfrom(client_fd, buf, sizeof(buf), 0, NULL, NULL);
        if (n < 9) continue;
        
        FramePacket pkt;
        deserializePacket(pkt, buf);
        
        if (pkt.type == END) {
            cout << "Received end of stream packet\n";
            break;
        }
        
        short* quantized = new short[width * height];
        LZ4_decompress_safe(
            (char*)pkt.data,
            (char*)quantized,
            pkt.length,
            width * height * sizeof(short)
        );
        
        // CPU IDCT
        uint8_t* pixels = cpu_idct_frame(quantized, width, height, Q);
        
        // Send to ffplay (live playback)
        if (ffplay) {
            fwrite(pixels, 1, width * height, ffplay);
            fflush(ffplay);
        }
        
        // Save to file
        cv::Mat frame(height, width, CV_8UC1, pixels);
        writer.write(frame);
        
        delete[] quantized;
        delete[] pixels;
        
        frames_received++;
        
        if (frames_received % 100 == 0) {
            cout << "\rDecoded " << frames_received << " frames..." << flush;
        }
    }

    if (ffplay) pclose(ffplay);
    writer.release();
    close(client_fd);

    cout << "\n\nTotal frames: " << frames_received << "\n";
    cout << "Video saved to: " << output_filename << "\n";
    
    return 0;
}
