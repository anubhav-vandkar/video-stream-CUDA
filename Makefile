OPENCV_INCLUDE = -I/usr/include/opencv4
CUDA_INCLUDE = -I/usr/local/cuda/include
OPENCV_LIBS = -lopencv_core -lopencv_videoio -lopencv_imgproc -lopencv_imgcodecs
NVCC_FLAGS = -arch=sm_75
CXX_FLAGS = -std=c++17 -O3 -mavx2 -mfma

all: check_cuda

check_cuda:
	@if command -v nvcc >/dev/null 2>&1; then \
		echo "CUDA detected - building GPU version"; \
		$(MAKE) server_gpu; \
	else \
		echo "CUDA not detected - building CPU-only version"; \
		$(MAKE) server_cpu; \
	fi
	@$(MAKE) client

server_gpu: dct/gpu_dct.o dct/gpu_quant.o dct/gpu_idct.o server.cpp
	@echo "Building GPU-enabled server..."
	g++ $(CXX_FLAGS) -DUSE_GPU -c server.cpp -o server.o \
	    $(OPENCV_INCLUDE) $(CUDA_INCLUDE)
	nvcc $(NVCC_FLAGS) dct/gpu_dct.o dct/gpu_quant.o dct/gpu_idct.o server.o -o server \
	    $(OPENCV_LIBS) -llz4 -L/usr/local/cuda/lib64 -lcudart
	@echo "GPU server built as 'server'"
	@rm -f server.o

server_cpu: server.cpp
	@echo "Building CPU-only server..."
	g++ $(CXX_FLAGS) server.cpp -o server \
	    $(OPENCV_INCLUDE) $(OPENCV_LIBS) -llz4
	@echo "CPU server built as 'server'"

client: client.cpp recv_queue.hpp recv_thread.hpp decoder_thread.hpp
	g++ $(CXX_FLAGS) client.cpp -o client \
	    $(OPENCV_INCLUDE) $(OPENCV_LIBS) -llz4

dct/gpu_dct.o: dct/gpu_dct.cu dct/gpu_dct.h
	nvcc $(NVCC_FLAGS) -c dct/gpu_dct.cu -o dct/gpu_dct.o $(OPENCV_INCLUDE)

dct/gpu_quant.o: dct/gpu_quant.cu dct/gpu_quant.h
	nvcc $(NVCC_FLAGS) -c dct/gpu_quant.cu -o dct/gpu_quant.o $(OPENCV_INCLUDE)

dct/gpu_idct.o: dct/gpu_idct.cu dct/gpu_idct.h
	nvcc $(NVCC_FLAGS) -c dct/gpu_idct.cu -o dct/gpu_idct.o $(OPENCV_INCLUDE)

gpu: server_gpu client

cpu: server_cpu client

clean:
	rm -f client server server.o dct/*.o

.PHONY: all check_cuda gpu cpu clean