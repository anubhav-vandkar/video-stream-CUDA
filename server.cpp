#include <iostream>
#include <fstream>
#include <string>
#include <arpa/inet.h>
#include <unistd.h>

#include "utils/udp_utils.hpp"
#include "utils/encoder_interface.hpp"

using namespace std;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        cerr << "Usage: ./server <file_directory> [--gpu|--cpu]\n";
#ifdef USE_GPU
        cerr << "  Compiled with GPU support\n";
        cerr << "  --gpu: Use GPU encoder (default)\n";
        cerr << "  --cpu: Force CPU encoder\n";
#else
        cerr << "  Compiled without GPU support (CPU only)\n";
#endif
        return 1;
    }

    string file_dir = argv[1];
    if (file_dir.back() != '/') {
        file_dir += '/';
    }
    
    // Parse encoder mode
    bool use_gpu = false;
    
#ifdef USE_GPU
    // Default to GPU if available
    use_gpu = true;
#else
    // No GPU available, always use CPU
    use_gpu = false;
    
    if (argc >= 3 && string(argv[2]) == "--gpu") {
        cerr << "Warning: --gpu requested but server compiled without CUDA\n";
        cerr << "Using CPU encoder instead\n";
    }
#endif

    // Socket setup
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
#ifdef USE_GPU
    cout << "GPU support: ENABLED\n";
#else
    cout << "GPU support: DISABLED (CPU-only build)\n";
#endif

    uint8_t buffer[150000];
    FramePacket pkt;
    sockaddr_in client{};
    socklen_t slen = sizeof(client);

    while (true) {
        cout << "\nWaiting for client request...\n";

        int n = recvfrom(server_fd, buffer, sizeof(buffer), 0, 
                        (sockaddr*)&client, &slen);
        
        if (n < 17) continue;
        
        deserializePacket(pkt, (char*)buffer);

        cout << "Received packet with seq=" << pkt.seq 
             << ", type=" << (int)pkt.type 
             << ", length=" << pkt.length << "\n";

        if (pkt.type != REQUEST) {
            cout << "Expected REQUEST packet\n";
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
            continue;
        }
        file_check.close();

        cout << "File found! Starting stream...\n";

        encode_and_send(full_path.c_str(), server_fd, client, use_gpu);
        
        cout << "Stream complete!\n";
    }

    close(server_fd);
    return 0;
}