#ifndef UDP_UTILS_H
#define UDP_UTILS_H

#include <cstdint>
#include <arpa/inet.h>
#include <cstring>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

enum FrameType : uint8_t {
    REQUEST = 0x01,
    DATA = 0x02,
    END = 0xFF
};

struct FramePacket {
    uint32_t seq;
    FrameType type;
    uint32_t length;
    char data[100000];
};

inline void serializePacket(const FramePacket &pkt, char *buf) {
    uint32_t seq_n = htonl(pkt.seq);
    uint32_t len_n = htonl(pkt.length);
    
    memcpy(buf, &seq_n, 4);
    buf[4] = pkt.type;
    memcpy(buf + 5, &len_n, 4);
    memcpy(buf + 9, pkt.data, pkt.length);
}

inline void deserializePacket(FramePacket &pkt, const char *buf) {
    uint32_t seq_n, len_n;
    uint8_t type;

    memcpy(&seq_n, buf, 4);
    memcpy(&type, buf + 4, 1);
    memcpy(&len_n, buf + 5, 4);

    pkt.seq = ntohl(seq_n);
    pkt.length = ntohl(len_n);
    pkt.type = static_cast<FrameType>(type);
    memcpy(pkt.data, buf + 9, pkt.length);
}

void sendRequest(int sock_fd, sockaddr_in &dest, const char* filename) {
    
    FramePacket pkt{};
    pkt.seq = 0;
    pkt.type = REQUEST;
    pkt.length = strlen(filename);
    memcpy(pkt.data, filename, pkt.length);
    
    char buffer[110000];
    serializePacket(pkt, buffer);
    
    size_t packet_size = 9 + pkt.length;
    sendto(sock_fd, buffer, packet_size, 0, (sockaddr*)&dest, sizeof(dest));
    
    std::cout << "Sent request for: " << filename << std::endl;
}

void sendFrame(int sock_fd, sockaddr_in &dest, const char *gpudata, uint32_t length, uint32_t seq) {

    std::cout << "Preparing to send frame " << seq << " (size: " << length << " bytes)" << std::endl;
    
    FramePacket pkt{};
    pkt.seq = seq;
    pkt.type = DATA;
    pkt.length = length;
    memcpy(pkt.data, gpudata, length);
    
    size_t packet_size = 9 + pkt.length;

    std::cout << "Sending frame " << seq << " (packet size: " << packet_size << " bytes)" << std::endl;
    sendto(sock_fd, &pkt, packet_size, 0, (sockaddr*)&dest, sizeof(dest));
    std::cout << "Sent frame " << seq << std::endl;
}

void sendStreamEnd(int sock_fd, sockaddr_in &dest, uint32_t seq) {
    
    FramePacket pkt{};
    pkt.seq = seq;
    pkt.type = END;
    pkt.length = 0;
    
    size_t packet_size = 9;
    sendto(sock_fd, &pkt, packet_size, 0, (sockaddr*)&dest, sizeof(dest));
}
#endif //UDP_UTILS_H
