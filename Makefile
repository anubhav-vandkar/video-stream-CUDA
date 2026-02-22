OPENCV_INCLUDE = -I/usr/include/opencv4
OPENCV_LIBS = -lopencv_core -lopencv_videoio -lopencv_imgproc -lopencv_imgcodecs
NVCC_FLAGS = -arch=sm_75

all: server client

server: dct/gpu_dct.o dct/gpu_quant.o dct/gpu_idct.o server.cpp
	nvcc -O3 -mavx2 -mfma $(NVCC_FLAGS) dct/gpu_dct.o dct/gpu_quant.o dct/gpu_idct.o server.cpp -o server.o \
	    $(OPENCV_INCLUDE) $(OPENCV_LIBS) -llz4

client: 
	g++ -std=c++17 -O3 -mavx2 -mfma client.cpp -o client.o \
	$(OPENCV_INCLUDE) $(OPENCV_LIBS) -llz4

dct/gpu_dct.o: dct/gpu_dct.cu dct/gpu_dct.h
	nvcc $(NVCC_FLAGS) -c dct/gpu_dct.cu -o dct/gpu_dct.o $(OPENCV_INCLUDE)

dct/gpu_quant.o: dct/gpu_quant.cu dct/gpu_quant.h
	nvcc $(NVCC_FLAGS) -c dct/gpu_quant.cu -o dct/gpu_quant.o $(OPENCV_INCLUDE)

dct/gpu_idct.o: dct/gpu_idct.cu dct/gpu_idct.h
	nvcc $(NVCC_FLAGS) -c dct/gpu_idct.cu -o dct/gpu_idct.o $(OPENCV_INCLUDE)

clean:
	rm -f client.o server.o dct/*.o