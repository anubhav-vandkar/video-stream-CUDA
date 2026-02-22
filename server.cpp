#include <vector>
#include <string>
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <arpa/inet.h>
#include <thread>
#include <unistd.h>

#include "utils/udp_utils.hpp"
#include "utils/gpu_utils.hpp"

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

    cout << "Server listening on port 9000" << endl;

    uint8_t buffer[150000];
    FramePacket pkt;
    sockaddr_in client{};
    socklen_t slen = sizeof(client);

    while (true) {
        cout << "Waiting for client request...\n";

        // Receive REQUEST
        int n = recvfrom(server_fd, buffer, sizeof(buffer), 0, (sockaddr*)&client, &slen);
        
        cout << "Received packet of size " << n << " bytes\n";
        
        if (n < 12) continue;  // Too small, ignore
        
        deserializePacket(pkt, (char*)buffer);

        cout << "Received packet with seq=" << pkt.seq 
             << ", type=" << (int)pkt.type 
             << ", length=" << pkt.length << "\n";

        if (pkt.type != REQUEST) {
            cout << "Expected REQUEST packet, got type " << (int)pkt.type << "\n";
            continue;
        }

        // Extract filename
        char requested_file[256] = {0};
        memcpy(requested_file, pkt.data, pkt.length);
        requested_file[pkt.length] = '\0';
        
        cout << "Client requested: " << requested_file << "\n";

        // Check if file exists
        string full_path = file_dir + requested_file;
        ifstream file_check(full_path, ios::binary);
        if (!file_check) {
            cerr << "File not found: " << full_path << "\n";
            // TODO: Send error packet
            continue;
        }
        file_check.close();

        cout << "File found!";

        // Open video
        cv::VideoCapture cap(full_path);
        if (!cap.isOpened()) {
            cerr << "Cannot open video\n";
            continue;
        }

        uint32_t seq = 0;

        cout<< "Starting video encoding and streaming...\n";

        encode_video_gpu(full_path.c_str(), server_fd, client);
        
        cout << "Stream complete!\n\n";
    }

    close(server_fd);
    return 0;
}