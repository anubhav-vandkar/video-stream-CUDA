# GPU Video Encoder - Student Project

Simple C++/CUDA video encoder converted from Python. Demonstrates:
- GPU-accelerated DCT transform
- Quantization for compression
- LZ4 compression (instead of RLE)
- Inverse DCT for reconstruction

## Project Structure

```
student_project/
├── cpu_utils.h           # CPU helper functions
├── dct/
│   ├── gpu_dct.cu        # GPU DCT kernel
│   ├── gpu_dct.h
│   ├── gpu_quant.cu      # Quantization kernel
│   ├── gpu_quant.h
│   ├── gpu_idct.cu       # Inverse DCT kernel
│   └── gpu_idct.h
├── main.cpp              # Encoder main program
├── reconstruction.cpp    # Decoder main program
├── Makefile              # Build system
└── README.md             # This file
```

## Key Changes from Python Version

### 1. Fixed Memory Leak
**Python (bad):**
```python
q_copy = cuda.mem_alloc(h*w*2)  # New allocation every frame!
quantized_frames.append((q_copy, (h, w)))
```

**C++ (fixed):**
```cpp
// Allocate ONCE before loop
cudaMalloc(&d_quantized, width * height * sizeof(short));

// Reuse in loop - no new allocations!
```

### 2. Replaced RLE with LZ4
**Why?** RLE doesn't parallelize well (as you learned). LZ4 is:
- Fast on CPU (~0.5ms for small data)
- Better compression than simple RLE
- Battle-tested library

### 3. Same Kernel Structure
The CUDA kernels are nearly identical to your Python PyCUDA version:
- Same shared memory usage
- Same constant memory for DCT matrix
- Same block/grid dimensions

## Building

### Prerequisites
```bash
# Ubuntu/Debian
sudo apt install build-essential cuda-toolkit
sudo apt install libopencv-dev liblz4-dev

# Check your GPU compute capability
nvidia-smi
# Then update CUDA_ARCH in Makefile if needed
```

### Compile
```bash
make
```

This creates two executables:
- `encoder` - Compresses video
- `decoder` - Reconstructs video

## Usage

### Encode a video
```bash
./encoder input_video.mp4
```

This creates compressed frames in `output_gpu/frame_XXXXX.lz4`

### Decode video
```bash
./decoder output_gpu reconstructed.mp4 1920 1080
```

Replace 1920 1080 with your video's actual resolution.

## Performance Tips

1. **GPU Architecture**: Update `CUDA_ARCH` in Makefile
   - GTX 16XX, RTX 20XX: sm_75
   - RTX 30XX: sm_86
   - RTX 40XX: sm_89

2. **Video Size**: Larger videos = better GPU utilization

3. **Quality**: Change `QUANTIZATION` constant
   - Lower = better quality, larger files
   - Higher = worse quality, smaller files

## Understanding the Code

### Flow: Encoding
```
Video Frame (OpenCV)
  → Convert to grayscale
  → Copy to GPU
  → GPU DCT transform (parallel!)
  → GPU Quantization (parallel!)
  → Copy back to CPU
  → LZ4 compression (sequential, but fast)
  → Save to file
```

### Flow: Decoding
```
Load compressed file
  → LZ4 decompression
  → Copy to GPU
  → GPU Dequantization
  → GPU Inverse DCT
  → Copy back to CPU
  → Write to video (OpenCV)
```

## Common Issues

### "nvcc: command not found"
Add CUDA to PATH:
```bash
export PATH=/usr/local/cuda/bin:$PATH
```

### "cannot find -llz4"
Install LZ4:
```bash
sudo apt install liblz4-dev
```

### "OpenCV not found"
Install OpenCV:
```bash
sudo apt install libopencv-dev
```

### Low performance
- Check GPU is actually being used: `nvidia-smi` during encoding
- Verify CUDA_ARCH matches your GPU
- Try with larger video for better GPU utilization

## Next Steps

1. **Add timing**: Measure DCT time vs LZ4 time separately
2. **Batch processing**: Process multiple frames in parallel
3. **QUIC integration**: Stream compressed frames over network
4. **Quality metrics**: Add PSNR calculation

## Learning Resources

- CUDA Programming Guide: https://docs.nvidia.com/cuda/
- DCT Explanation: https://en.wikipedia.org/wiki/Discrete_cosine_transform
- LZ4 Library: https://github.com/lz4/lz4
