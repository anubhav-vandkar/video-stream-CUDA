#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <map>
#include <opencv2/opencv.hpp>
#include <lz4.h>

#include "utils/udp_utils.hpp"
#include "utils/cpu_utils.hpp"
#include "config.hpp"
#include "recv_queue.hpp"
#include "recv_thread.hpp"
#include "decoder_thread.hpp"
#include <thread>

using namespace std;

// FRAGMENTATION IMPLEMENTATION
// frame_id, chunk_id, data
map<uint32_t, map<uint32_t, vector<uint8_t>>> chunk_buffer;
// frame_id, total_chunks
map<uint32_t, uint32_t> frame_total_chunks;

int main(int argc, char* argv[]){
    if (argc != 6) {
        cerr << "Usage: ./client <dest-ip> <remote-filename> <output-filename> <width> <height>\n";
        return 1;
    }

    const char* dest_ip = argv[1];
    const char* remote_filename = argv[2];
    const char* output_filename = argv[3];
    const int width = atoi(argv[4]);
    const int height = atoi(argv[5]);

    // Socket setup
    int client_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_fd < 0) {
        cerr << "Socket creation failed\n";
        return 1;
    }

    int rcvbuf = 10 * 1024 * 1024;  // 10 MB
    setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(9000);
    inet_pton(AF_INET, dest_ip, &dest.sin_addr);

    sendRequest(client_fd, dest, remote_filename);

    FILE* ffplay = popen(
        ("ffplay -f rawvideo -pixel_format gray -video_size "
         + to_string(width) + "x" + to_string(height)
         + " -framerate " + to_string(FPS) + " -i - 2>/dev/null").c_str(),
        "w"
    );
    if (!ffplay) cerr << "Warning: ffplay not available. Saving to file only.\n";

    // Video writer output
    cv::VideoWriter writer(
        output_filename,
        cv::VideoWriter::fourcc('m','p','4','v'),
        FPS,
        cv::Size(width, height),
        false  // grayscale
    );

    cout << "Starting multithreaded receive and decode...\n";
    if (ffplay) cout << "Live playback\n";

    // Create shared queue
    FrameQueue frame_queue(30);  // Buffer up to 30 frames

    // Create thread objects
    ReceiverThread receiver(client_fd, width, height, frame_queue);
    DecoderThread decoder(frame_queue, ffplay, writer, width, height, Q);

    // Launch threads
    thread receiver_thread([&receiver]() { receiver.run(); });
    thread decoder_thread([&decoder]() { decoder.run(); });

    // Wait for completion
    receiver_thread.join();
    decoder_thread.join();

    // Cleanup
    if (ffplay) pclose(ffplay);
    writer.release();
    close(client_fd);

    cout << "\nStreaming complete!\n";
    cout << "Video saved to: " << output_filename << "\n";

    return 0;
}