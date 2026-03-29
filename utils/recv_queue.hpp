#ifndef RECV_QUEUE_HPP
#define RECV_QUEUE_HPP

#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdint>

using namespace std;

// Frame data after reassembly
struct CompressedFrame {
    uint32_t seq;
    vector<uint8_t> data;
};

// Shared queue between receiver and decoder
class FrameQueue {
private:
    queue<CompressedFrame> frame_queue;
    mutex frame_queue_mutex;
    condition_variable cv;
    bool done;
    size_t max_size;

public:
    FrameQueue(size_t max_size = 30) 
        : done(false), max_size(max_size) {}
    
    // Producer
    void push(CompressedFrame&& frame) {
        unique_lock<mutex> lock(frame_queue_mutex);
        
        // Wait if queue full
        cv.wait(lock, [this]{ 
            return frame_queue.size() < max_size || done; 
        });
        
        if (!done) {
            frame_queue.push(move(frame));
            cv.notify_one();  // Wake consumer
        }
    }
    
    // Consumer 
    bool pop(CompressedFrame& frame) {
        unique_lock<mutex> lock(frame_queue_mutex);
        
        // Wait for data or done signal
        cv.wait(lock, [this]{ 
            return !frame_queue.empty() || done; 
        });
        
        // Check if actually done and empty
        if (done && frame_queue.empty()) {
            return false;
        }
        
        frame = move(frame_queue.front());
        frame_queue.pop();
        
        cv.notify_one();
        return true;
    }
    
    // Signal no more frames coming
    void set_done() {
        unique_lock<mutex> lock(frame_queue_mutex);
        done = true;
        cv.notify_all();
    }
    
    // Get current size (for stats)
    size_t size() {
        unique_lock<mutex> lock(frame_queue_mutex);
        return frame_queue.size();
    }
};

#endif // RECV_QUEUE_HPP