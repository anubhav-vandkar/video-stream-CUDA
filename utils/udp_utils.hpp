#ifndef UDP_UTILS_H
#define UDP_UTILS_H

#include <cstdint>
#include <arpa/inet.h>
#include <cstring>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#define CHUNK_SIZE 60000

enum FrameType : uint8_t {
    REQUEST = 0x01,
    DATA = 0x02,
    END = 0xFF
};

struct FramePacket {
    uint32_t seq;
    FrameType type;
    uint32_t length;

    uint32_t chunk_id;
    uint32_t chunk_total;

    char data[CHUNK_SIZE];
};

inline void serializePacket(const FramePacket &pkt, uint8_t *buf) {
    uint32_t seq_n = htonl(pkt.seq);
    uint32_t len_n = htonl(pkt.length);

    uint32_t chunk_id_n = htonl(pkt.chunk_id);
    uint32_t chunk_total_n = htonl(pkt.chunk_total);
    
    memcpy(buf, &seq_n, 4);
    buf[4] = pkt.type;
    memcpy(buf + 5, &len_n, 4);
    memcpy(buf + 9, &chunk_id_n, 4);
    memcpy(buf + 13, &chunk_total_n, 4);
    memcpy(buf + 17, pkt.data, pkt.length);
}

inline void deserializePacket(FramePacket &pkt, const char *buf) {
    uint32_t seq_n, len_n, chunk_id_n, chunk_total_n;
    uint8_t type;

    memcpy(&seq_n, buf, 4);
    memcpy(&type, buf + 4, 1);
    memcpy(&len_n, buf + 5, 4);
    memcpy(&chunk_id_n, buf + 9, 4);
    memcpy(&chunk_total_n, buf + 13, 4);

    pkt.seq = ntohl(seq_n);
    pkt.length = ntohl(len_n);
    pkt.type = static_cast<FrameType>(type);
    pkt.chunk_id = ntohl(chunk_id_n);
    pkt.chunk_total = ntohl(chunk_total_n);
    memcpy(pkt.data, buf + 17, pkt.length);
}

void sendRequest(int sock_fd, sockaddr_in &dest, const char* filename) {
    
    FramePacket pkt{};
    pkt.seq = 0;
    pkt.type = REQUEST;
    pkt.length = strlen(filename);
    memcpy(pkt.data, filename, pkt.length);
    
    uint8_t buffer[pkt.length + 17];
    serializePacket(pkt, buffer);
    
    size_t packet_size = 17 + pkt.length;
    sendto(sock_fd, buffer, packet_size, 0, (sockaddr*)&dest, sizeof(dest));
    
    std::cout << "Sent request for: " << filename << std::endl;
}

void sendFrame(int sock_fd, sockaddr_in &dest, const char *gpudata, uint32_t length, uint32_t seq) {

    std::cout << "Preparing to send frame " << seq << " (size: " << length << " bytes)" << std::endl;

    uint32_t total_chunks = (length + CHUNK_SIZE - 1) / CHUNK_SIZE;
    
    for (uint32_t chunk_id = 0; chunk_id < total_chunks; ++chunk_id) {

        FramePacket pkt{};
        pkt.seq = seq;
        pkt.type = DATA;
        pkt.length = (chunk_id == total_chunks - 1) ? (length - chunk_id * CHUNK_SIZE) : CHUNK_SIZE;
        pkt.chunk_id = chunk_id;
        pkt.chunk_total = total_chunks;
        memcpy(pkt.data, gpudata + chunk_id * CHUNK_SIZE, pkt.length);

        uint8_t buffer[CHUNK_SIZE + 17];
        serializePacket(pkt, buffer);
        
        sendto(sock_fd, buffer, 17 + pkt.length, 0, (sockaddr*)&dest, sizeof(dest));
    }
    std::cout << "Sent frame " << seq << std::endl;
}

void sendStreamEnd(int sock_fd, sockaddr_in &dest, uint32_t seq) {
    
    FramePacket pkt{};
    pkt.seq = seq;
    pkt.type = END;
    pkt.length = 0;
    
    size_t packet_size = 17;
    uint8_t buffer[packet_size];
    serializePacket(pkt, buffer);
    sendto(sock_fd, buffer, packet_size, 0, (sockaddr*)&dest, sizeof(dest));
}
#endif //UDP_UTILS_H
