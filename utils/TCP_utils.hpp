#pragma once
#include <cstdint>
#include <arpa/inet.h>
#include <cstring>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

using namespace std;

#define SYN 1
#define ACK 2
#define FIN 4

const int MSS = 1472;
const int ACK_FREQUENCY = 4;  // Send ACK every N packets
using ms = chrono::milliseconds;

struct TCPHeader {
    uint32_t seq;
    uint32_t ack;
    uint8_t flags;
    uint16_t length;
};

inline void serializeHeader(const TCPHeader &hdr, char *buf) {
    uint32_t seq_n = htonl(hdr.seq);
    uint32_t ack_n = htonl(hdr.ack);
    uint16_t len_n = htons(hdr.length);

    memcpy(buf,        &seq_n, 4);
    memcpy(buf + 4,    &ack_n, 4);
    memcpy(buf + 8,    &hdr.flags, 1);
    memcpy(buf + 9,    &len_n, 2);
}

inline void deserializeHeader(TCPHeader &hdr, const char *buf) {
    uint32_t seq_n, ack_n;
    uint16_t len_n;

    memcpy(&seq_n, buf, 4);
    memcpy(&ack_n, buf + 4, 4);
    memcpy(&hdr.flags, buf + 8, 1);
    memcpy(&len_n, buf + 9, 2);

    hdr.seq = ntohl(seq_n);
    hdr.ack = ntohl(ack_n);
    hdr.length = ntohs(len_n);
}

void sendFileData(int sockfd, sockaddr_in &dest, const char *filename) {

    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "Cannot open file\n";
        return;
    }

    vector<vector<char>> segments;
    vector<uint32_t> seqs;

    uint32_t seq = 1;
    char block[MSS];

    while (true) {
        file.read(block, MSS);
        int n = file.gcount();
        if (n <= 0) break;

        TCPHeader hdr{};
        hdr.seq = seq;
        hdr.ack = 0;
        hdr.flags = 0;
        hdr.length = n;

        vector<char> pkt(11 + n);
        serializeHeader(hdr, pkt.data());
        memcpy(pkt.data() + 11, block, n);

        segments.push_back(pkt);
        seqs.push_back(seq);

        seq += n;
    }

    int total = segments.size();
    int base = 0;
    int next = 0;
    int WINDOW = 4;

    char buf[1500];
    socklen_t dlen = sizeof(dest);
    TCPHeader ackhdr;

    bool timer_running = false;
    auto timer_start = chrono::steady_clock::now();
    const int TIMEOUT = 1500; // ms

    while (base < total) {
        // Send packets within window
        while (next < total && next - base < WINDOW) {
            sendto(sockfd, segments[next].data(),
                   segments[next].size(), 0,
                   (sockaddr*)&dest, dlen);

            if (!timer_running) {
                timer_running = true;
                timer_start = chrono::steady_clock::now();
            }

            next++;
        }

        // WAIT FOR ACK or TIMEOUT 
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);

        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 20000;  // 200ms polling

        int rv = select(sockfd+1, &fds, NULL, NULL, &tv);

        if (rv > 0) {
            int n = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
            if (n >= 11) {
                deserializeHeader(ackhdr, buf);
                uint32_t acknum = ackhdr.ack;
                cout << "Server received ACK=" << acknum << " (base=" << base << ", next=" << next << ")\n";

                // Slide window
                int old_base = base;
                while (base < total) {
                    uint32_t seg_seq = seqs[base];
                    uint32_t payload = (uint32_t)(segments[base].size() - 11);

                    if (acknum >= seg_seq + payload) {
                        base++;
                    } else {
                        break;
                    }
                }
                if (base > old_base) {
                    cout << "  Window advanced: base " << old_base << " -> " << base << "\n";
                }
                
                // Reset or stop timer on ACK received
                if (base == next) {
                    timer_running = false;
                } else {
                    timer_running = true;
                    timer_start = chrono::steady_clock::now();
                }
            }
        }

        // CHECK FOR TIMEOUT
        if (timer_running) {
            auto now = chrono::steady_clock::now();
            int elapsed = chrono::duration_cast<ms>(now - timer_start).count();

            if (elapsed > TIMEOUT) {
                cout << "TIMEOUT — retransmitting " << WINDOW << " segments\n";

                // Retransmit ALL unACKed segments (Go-Back-N)
                for (int i = base; i < next; i++) {
                    sendto(sockfd, segments[i].data(),
                           segments[i].size(), 0,
                           (sockaddr*)&dest, dlen);
                }

                timer_start = chrono::steady_clock::now();
            }
        }
    }
    
    TCPHeader fin{};
    fin.seq = seq;
    fin.ack = 0;
    fin.flags = FIN;
    fin.length = 0;

    char finbuf[11];
    serializeHeader(fin, finbuf);
    sendto(sockfd, finbuf, 11, 0, (sockaddr*)&dest, dlen);

    cout << "GBN file send complete.\n";
}


void receiveFileData(int sockfd, sockaddr_in &sender, const char *outfile) {
    vector<char> data;
    uint32_t expected_seq = 1;

    char buf[1500];
    socklen_t slen = sizeof(sender);
    TCPHeader hdr;

    while (true) {
        int n = recvfrom(sockfd, buf, sizeof(buf), 0,
                         (sockaddr*)&sender, &slen);
        if (n < 11)
            continue;

        deserializeHeader(hdr, buf);

        if (hdr.flags & FIN) {
            cout << "Received FIN\n";
            break;
        }

        if (hdr.seq == expected_seq) {
            // In-order packet
            data.insert(data.end(), buf + 11, buf + 11 + hdr.length);
            expected_seq += hdr.length;
            cout << "Received " << hdr.length << " bytes\n";  // <-- ADD THIS
        }

        // Send ACK for every packet
        TCPHeader ack{};
        ack.seq = 0;
        ack.ack = expected_seq;
        ack.flags = ACK;
        ack.length = 0;

        char ackbuf[11];
        serializeHeader(ack, ackbuf);
        sendto(sockfd, ackbuf, 11, 0, (sockaddr*)&sender, slen);
    }

    // write final file
    ofstream out(outfile, ios::binary);
    out.write(data.data(), data.size());
    out.close();

    cout << "Saved file of " << data.size() << " bytes\n";
}
