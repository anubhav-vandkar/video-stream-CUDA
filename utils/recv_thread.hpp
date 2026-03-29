#ifndef RECEIVER_THREAD_HPP
#define RECEIVER_THREAD_HPP

#include <iostream>
#include <map>
#include <vector>
#include <arpa/inet.h>
#include "recv_queue.hpp"
#include "utils/udp_utils.hpp"

using namespace std;

class ReceiverThread {
private:
    int sockfd;
    int width;
    int height;
    FrameQueue& frame_queue;
    
    // Chunk reassembly state
    map<uint32_t, map<uint32_t, vector<uint8_t>>> chunk_buffer;
    map<uint32_t, uint32_t> frame_total_chunks;
    
    uint32_t frames_received = 0;

public:
    ReceiverThread(int sockfd, int width, int height, FrameQueue& queue)
        : sockfd(sockfd), width(width), height(height), frame_queue(queue) {}
    
    void run() {
        uint8_t buf[CHUNK_SIZE + 17];
        
        cout << "Receiver thread started\n";
        
        while (true) {
            // Receive packet
            int n = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
            if (n < 17) continue;
            
            FramePacket pkt;
            deserializePacket(pkt, (char*)buf);
            
            // Check for END
            if (pkt.type == END) {
                cout << "Received END packet\n";
                break;
            }
            
            // Store chunk
            chunk_buffer[pkt.seq][pkt.chunk_id] = 
                vector<uint8_t>(pkt.data, pkt.data + pkt.length);
            frame_total_chunks[pkt.seq] = pkt.chunk_total;
            
            // Check if frame complete
            if (chunk_buffer[pkt.seq].size() == frame_total_chunks[pkt.seq]) {
                
                // Reassemble
                vector<uint8_t> full_frame;
                bool complete = true;
                
                for (uint32_t i = 0; i < pkt.chunk_total; i++) {
                    if (chunk_buffer[pkt.seq].count(i) == 0) {
                        cerr << "Missing chunk " << i 
                                  << " for frame " << pkt.seq << "\n";
                        complete = false;
                        break;
                    }
                    auto& chunk = chunk_buffer[pkt.seq][i];
                    full_frame.insert(full_frame.end(), 
                                    chunk.begin(), chunk.end());
                }
                
                if (complete) {
                    // Push to decode queue
                    CompressedFrame cf;
                    cf.seq = pkt.seq;
                    cf.data = move(full_frame);
                    
                    frame_queue.push(move(cf));
                    
                    frames_received++;
                    // if (frames_received % 100 == 0) {
                    //     cout << "Received " << frames_received << " frames (queue: " << frame_queue.size() << ")\n";
                    // }
                }
                
                // Cleanup
                chunk_buffer.erase(pkt.seq);
                frame_total_chunks.erase(pkt.seq);
            }
        }
        
        // Signal completion
        frame_queue.set_done();
        
        cout << "Receiver thread done. Total: " << frames_received << " frames\n";
    }
};

#endif // RECEIVER_THREAD_HPP