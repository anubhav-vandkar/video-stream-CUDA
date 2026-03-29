#ifndef SHARED_QUEUE_HPP
#define SHARED_QUEUE_HPP

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
    queue<CompressedFrame> queue;
    mutex mutex;
    condition_variable cv;
    bool done;
    size_t max_size;

public:
    FrameQueue(size_t max_size = 30) 
        : done(false), max_size(max_size) {}
    
    // Producer
    void push(CompressedFrame&& frame) {
        unique_lock<std::mutex> lock(mutex);
        
        // Wait if queue full
        cv.wait(lock, [this]{ 
            return queue.size() < max_size || done; 
        });
        
        if (!done) {
            queue.push(move(frame));
            cv.notify_one();  // Wake consumer
        }
    }
    
    // Consumer 
    bool pop(CompressedFrame& frame) {
        unique_lock<std::mutex> lock(mutex);
        
        // Wait for data or done signal
        cv.wait(lock, [this]{ 
            return !queue.empty() || done; 
        });
        
        // Check if actually done and empty
        if (done && queue.empty()) {
            return false;
        }
        
        frame = move(queue.front());
        queue.pop();
        
        cv.notify_one();
        return true;
    }
    
    // Signal no more frames coming
    void set_done() {
        unique_lock<std::mutex> lock(mutex);
        done = true;
        cv.notify_all();
    }
    
    // Get current size (for stats)
    size_t size() {
        unique_lock<std::mutex> lock(mutex);
        return queue.size();
    }
};

#endif // SHARED_QUEUE_HPP