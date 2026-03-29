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

    // Video output
    cv::VideoWriter writer(
        output_filename,
        cv::VideoWriter::fourcc('m','p','4','v'),
        FPS,
        cv::Size(width, height),
        false  // grayscale
    );

    cout << "Receiving and decoding video...\n";
    if (ffplay) cout << "Live playback started in ffplay window\n";

    // Receive buffer 
    char buf[17 + CHUNK_SIZE];

    uint32_t frames_received = 0;

    while (true) {
        int n = recvfrom(client_fd, buf, sizeof(buf), 0, NULL, NULL);

        if (n < 17) continue;

        FramePacket pkt;
        deserializePacket(pkt, buf);

        if (pkt.type == END) {
            cout << "Received END packet — stream complete\n";
            break;
        }

        if (pkt.type != DATA) continue;  // Ignore unexpected packet types

        chunk_buffer[pkt.seq][pkt.chunk_id] =
            vector<uint8_t>(pkt.data, pkt.data + pkt.length);
        frame_total_chunks[pkt.seq] = pkt.chunk_total;

        if (chunk_buffer[pkt.seq].size() != frame_total_chunks[pkt.seq])
            continue;

        vector<uint8_t> compressed_frame;
        compressed_frame.reserve(pkt.chunk_total * CHUNK_SIZE);
        for (uint32_t i = 0; i < pkt.chunk_total; i++) {
            auto& chunk = chunk_buffer[pkt.seq][i];
            compressed_frame.insert(compressed_frame.end(), chunk.begin(), chunk.end());
        }

        // cout << "Frame " << pkt.seq << ": reassembled " << compressed_frame.size()
        //      << " bytes from " << pkt.chunk_total << " chunk(s)\n";

        const int decompressed_size = width * height * sizeof(short);
        vector<short> quantized(width * height);

        int result = LZ4_decompress_safe(
            (const char*)compressed_frame.data(),
            (char*)quantized.data(),
            (int)compressed_frame.size(),
            decompressed_size
        );

        if (result < 0) {
            cerr << "LZ4 decompression failed for frame " << pkt.seq
                 << " (error " << result << ", input " << compressed_frame.size() << " bytes)\n";
            // Clean up and skip this frame
            chunk_buffer.erase(pkt.seq);
            frame_total_chunks.erase(pkt.seq);
            continue;
        }

        // CPU IDCT
        uint8_t* pixels = cpu_idct_frame(quantized.data(), width, height);

        // Live playback
        if (ffplay) {
            fwrite(pixels, 1, width * height, ffplay);
            fflush(ffplay);
        }

        // Save frame to output
        cv::Mat frame(height, width, CV_8UC1, pixels);
        writer.write(frame);

        delete[] pixels;

        chunk_buffer.erase(pkt.seq);
        frame_total_chunks.erase(pkt.seq);

        frames_received++;
        // if (frames_received % 100 == 0)
        //     cout << "Decoded " << frames_received << " frames...\n";
    }

    if (ffplay) pclose(ffplay);
    writer.release();
    close(client_fd);

    cout << "Total frames: " << frames_received << "\n";
    cout << "Video saved to: " << output_filename << "\n";

    return 0;
}