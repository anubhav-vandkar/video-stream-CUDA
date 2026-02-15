#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <unistd.h>
#include <arpa/inet.h>
#include "utils/TCP_utils.hpp"
#include "utils/udp_utils.hpp"
#include <thread>
#include <opencv2/opencv.hpp>
#include <cuda_runtime.h>
#include <lz4.h>
#include "cpu_utils.h"
#include "dct/gpu_dct.h"
#include "dct/gpu_quant.h"

using namespace std;

using ms = chrono::milliseconds;

int notmain(int argc, char *argv[]){

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
        cerr << "Socket creation failed\n";
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

    cout << "Server listening on port 9000";

    char title_buffer[1500];
    TCPHeader hdr;
    sockaddr_in client{};
    socklen_t slen = sizeof(client);

    while(1) {
        cout << "Waiting for client connection";

        // Wait for SYN
        while (true) {
            int n = recvfrom(server_fd, title_buffer, sizeof(title_buffer), 0, 
                            (sockaddr*)&client, &slen);
            if (n < 11) 
                continue;

            deserializeHeader(hdr, title_buffer);

            if (hdr.flags & SYN) {
                cout << "Server: Received SYN from client\n";
                break;
            }
        }

        // Send SYN-ACK
        TCPHeader synack{};
        synack.seq = 1000;
        synack.ack = hdr.seq + 1;
        synack.flags = SYN | ACK;
        synack.length = 0;

        serializeHeader(synack, title_buffer);
        sendto(server_fd, title_buffer, 11, 0, (sockaddr*)&client, slen);
        cout << "Server: Sent SYN-ACK\n";

        // Wait for final ACK
        bool handshake_done = false;
        auto handshake_start = chrono::steady_clock::now();
        
        while (!handshake_done) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(server_fd, &fds);

            timeval tv{0, 200000}; // 200ms
            int rv = select(server_fd+1, &fds, NULL, NULL, &tv);

            if (rv > 0) {
                int n = recvfrom(server_fd, title_buffer, sizeof(title_buffer), 0, 
                                (sockaddr*)&client, &slen);
                if (n >= 11) {
                    deserializeHeader(hdr, title_buffer);
                    if (hdr.flags & ACK) {
                        cout << "Server: Received final ACK, connection established!\n";
                        handshake_done = true;
                        break;
                    }
                }
            }

            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<ms>(now - handshake_start).count() > 3000) {
                cout << "Server: Handshake timeout\n";
                break;
            }
        }

        if (!handshake_done) continue;

        // RECEIVE FILENAME REQUEST 
        cout << "Server: Waiting for filename request...\n";
        
        bool got_filename = false;
        char requested_file[256] = {0};
        auto filename_start = chrono::steady_clock::now();

        while (!got_filename) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(server_fd, &fds);

            timeval tv{0, 200000}; // 200ms
            int rv = select(server_fd+1, &fds, NULL, NULL, &tv);

            if (rv > 0) {
                int n = recvfrom(server_fd, title_buffer, sizeof(title_buffer), 0, 
                                (sockaddr*)&client, &slen);
                if (n >= 11) {
                    deserializeHeader(hdr, title_buffer);
                    
                    // filename request
                    if (!(hdr.flags & SYN) && !(hdr.flags & FIN) && hdr.length > 0) {
                        memcpy(requested_file, title_buffer + 11, hdr.length);
                        requested_file[hdr.length] = '\0';
                        cout << "Server: Received filename request: " << requested_file << "\n";
                        got_filename = true;
                        break;
                    }
                }
            }

            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<ms>(now - filename_start).count() > 5000) {
                cout << "Server: Timeout waiting for filename\n";
                break;
            }
        }

        if (!got_filename) continue;

        // SEND FILE DATA
        string full_path = file_dir + requested_file;
        
        ifstream file_check(full_path, ios::binary);
        if (!file_check) {
            cerr << "Server: File not found: " << full_path << "\n";
            continue;
        }
        file_check.close();

        cout << "Server: Sending file: " << full_path << "\n";
        sockaddr_in client_copy = client; // Make a copy for sendFileData
        sendFileData(server_fd, client_copy, full_path.c_str());
        cout << "Server: File sent complete!\n";
    }

    close(server_fd);
    return 0;
}
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
        cerr << "Socket creation failed\n";
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

    cout << "Server listening on port 9000\n";

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

        // ===== SEND DUMMY DATA FOR NOW =====
        uint32_t seq = 0;
        
        for (int i = 0; i < 100; i++) {
            // Create dummy data
            uint8_t dummy_data[5000];
            for (int j = 0; j < 5000; j++) {
                dummy_data[j] = (i + j) % 256;
            }
            
            // Send frame
            sendFrame(server_fd, client, (char*)dummy_data, 5000, seq++);
            
            cout << "Sent frame " << (seq-1) << "\n";
            
            // Rate limit to 30 FPS
            this_thread::sleep_for(chrono::milliseconds(33));
        }
        
        // Send END
        sendStreamEnd(server_fd, client, seq);
        
        cout << "Stream complete!\n\n";
    }

    close(server_fd);
    return 0;
}