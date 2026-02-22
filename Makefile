OPENCV_INCLUDE = -I/usr/include/opencv4
CUDA_INCLUDE = -I/usr/local/cuda/include
OPENCV_LIBS = -lopencv_core -lopencv_videoio -lopencv_imgproc -lopencv_imgcodecs
NVCC_FLAGS = -arch=sm_75
CXX_FLAGS = -std=c++17 -O3 -mavx2 -mfma

all: server client

server.o: server.cpp
	g++ $(CXX_FLAGS) -c server.cpp -o server.o \
	    $(OPENCV_INCLUDE) $(CUDA_INCLUDE)

server: dct/gpu_dct.o dct/gpu_quant.o dct/gpu_idct.o server.o
	nvcc $(NVCC_FLAGS) dct/gpu_dct.o dct/gpu_quant.o dct/gpu_idct.o server.o -o server \
	    $(OPENCV_LIBS) -llz4 -L/usr/local/cuda/lib64 -lcudart

client: client.cpp
	g++ $(CXX_FLAGS) client.cpp -o client \
	    $(OPENCV_INCLUDE) $(OPENCV_LIBS) -llz4

dct/gpu_dct.o: dct/gpu_dct.cu dct/gpu_dct.h
	nvcc $(NVCC_FLAGS) -c dct/gpu_dct.cu -o dct/gpu_dct.o $(OPENCV_INCLUDE)

dct/gpu_quant.o: dct/gpu_quant.cu dct/gpu_quant.h
	nvcc $(NVCC_FLAGS) -c dct/gpu_quant.cu -o dct/gpu_quant.o $(OPENCV_INCLUDE)

dct/gpu_idct.o: dct/gpu_idct.cu dct/gpu_idct.h
	nvcc $(NVCC_FLAGS) -c dct/gpu_idct.cu -o dct/gpu_idct.o $(OPENCV_INCLUDE)

clean:
	rm -f client server server.o dct/*.o