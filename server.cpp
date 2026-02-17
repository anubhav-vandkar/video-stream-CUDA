#include <vector>
#include <string>
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <arpa/inet.h>
#include <thread>

#include "TCP_utils.hpp"
#include "udp_utils.hpp"
#include "gpu_utils.hpp"

using ms = chrono::milliseconds;

using namespace std;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        cerr << "Usage: ./server <file_directory>\n";
        return 1;
    }

    string file_dir = argv[1];
    if (file_dir.back() != '/') {
        file_dir += '/';
    }

    // Socket
    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd < 0) {
        cerr << "Socket creation failed" << endl;
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "Bind failed\n";
        return 1;
    }

    std::cout << "Server listening on port 9000" << endl;

    uint8_t buffer[150000];
    FramePacket pkt;
    sockaddr_in client{};
    socklen_t slen = sizeof(client);

    while (true) {
        std::cout << "Waiting for client request...\n";

        // Receive REQUEST
        int n = recvfrom(server_fd, buffer, sizeof(buffer), 0, (sockaddr*)&client, &slen);
        
        std::cout << "Received packet of size " << n << " bytes\n";
        
        if (n < 12) continue;  // Too small, ignore
        
        deserializePacket(pkt, (char*)buffer);

        std::cout << "Received packet with seq=" << pkt.seq 
             << ", type=" << (int)pkt.type 
             << ", length=" << pkt.length << "\n";

        if (pkt.type != REQUEST) {
            std::cout << "Expected REQUEST packet, got type " << (int)pkt.type << "\n";
            continue;
        }

        // Extract filename
        char requested_file[256] = {0};
        memcpy(requested_file, pkt.data, pkt.length);
        requested_file[pkt.length] = '\0';
        
        std::cout << "Client requested: " << requested_file << "\n";

        // Check if file exists
        string full_path = file_dir + requested_file;
        ifstream file_check(full_path, ios::binary);
        if (!file_check) {
            cerr << "File not found: " << full_path << "\n";
            // TODO: Send error packet
            continue;
        }
        file_check.close();

        std::cout << "File found!";

        // Open video
        cv::VideoCapture cap(full_path);
        if (!cap.isOpened()) {
            cerr << "Cannot open video\n";
            continue;
        }

        uint32_t seq = 0;

        encode_video_gpu(full_path.c_str(), server_fd, client);

        // for(size_t i = 0; i < frames.size(); i++) {
        //     char* frame_data = (char*)frames[i];
        //     uint32_t frame_size = 100000; // TODO: Pass actual size from encode_video_gpu

        //     sendFrame(server_fd, client, frame_data, frame_size, seq++);
        //     std::cout << "Sent frame " << i << " with seq " << (seq-1) << "\n";

        //     // Rate limit to ~30 FPS
        //     this_thread::sleep_for(chrono::milliseconds(33));
        // }

        // sendStreamEnd(server_fd, client, seq);
        
        std::cout << "Stream complete!\n\n";
    }

    close(server_fd);
    return 0;
}