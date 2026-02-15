#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include "utils/TCP_utils.hpp"
#include <fstream>
#include <vector>

using namespace std;

int main(int argc, char* argv[]){

    if (argc != 4) {
        cerr << "Usage: ./client <dest-ip> <remote-filename> <output-filename>\n";
        return 1;
    }

    const char *dest_ip = argv[1];
    const char *remote_filename = argv[2];
    const char *output_filename = argv[3];

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

    char buf[1500];
    socklen_t dlen = sizeof(dest);
    TCPHeader hdr;

    // Send SYN
    TCPHeader syn{};
    syn.seq = 1;
    syn.ack = 0;
    syn.flags = SYN;
    syn.length = 0;

    serializeHeader(syn, buf);
    sendto(client_fd, buf, 11, 0, (sockaddr*)&dest, dlen);
    cout << "Client: Sent SYN\n";

    // Wait for SYN-ACK
    auto last = clock::now();
    while (true) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(client_fd, &fds);

        timeval tv{0, 200000}; // 200ms
        int rv = select(client_fd+1, &fds, NULL, NULL, &tv);

        if (rv > 0) {
            int n = recvfrom(client_fd, buf, sizeof(buf), 0, (sockaddr*)&dest, &dlen);
            if (n < 11) continue;
            
            deserializeHeader(hdr, buf);

            if ((hdr.flags & SYN) && (hdr.flags & ACK)) {
                cout << "Client: Received SYN-ACK\n";
                break;
            }
        }

        auto now = clock::now();
        if (chrono::duration_cast<ms>(now - last).count() > 1500) {
            cout << "Client: Timeout — resending SYN\n";
            serializeHeader(syn, buf);
            sendto(client_fd, buf, 11, 0, (sockaddr*)&dest, dlen);
            last = clock::now();
        }
    }

    // Send final ACK
    TCPHeader ack{};
    ack.seq = hdr.ack;
    ack.ack = hdr.seq + 1;
    ack.flags = ACK;
    ack.length = 0;

    serializeHeader(ack, buf);
    sendto(client_fd, buf, 11, 0, (sockaddr*)&dest, dlen);
    cout << "Client: Connection established!\n";

    // SEND FILENAME REQUEST
    TCPHeader req{};
    req.seq = hdr.ack;
    req.ack = hdr.seq + 1;
    req.flags = 0; // Data packet
    req.length = strlen(remote_filename);

    serializeHeader(req, buf);
    memcpy(buf + 11, remote_filename, strlen(remote_filename));
    sendto(client_fd, buf, 11 + strlen(remote_filename), 0, (sockaddr*)&dest, dlen);
    cout << "Client: Sent filename request: " << remote_filename << "\n";

    // RECEIVE FILE DATA
    receiveFileData(client_fd, dest, output_filename);

    close(client_fd);
    return 0;
}
