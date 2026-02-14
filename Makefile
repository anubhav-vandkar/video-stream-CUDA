OPENCV_INCLUDE = -I/usr/include/opencv4
OPENCV_LIBS = -lopencv_core -lopencv_videoio -lopencv_imgproc -lopencv_imgcodecs

all: encoder decoder

encoder: dct/gpu_dct.o dct/gpu_quant.o dct/gpu_idct.o main.cpp
	nvcc dct/gpu_dct.o dct/gpu_quant.o dct/gpu_idct.o main.cpp -o encoder \
	    $(OPENCV_INCLUDE) $(OPENCV_LIBS) -llz4

decoder: dct/gpu_dct.o dct/gpu_quant.o dct/gpu_idct.o reconstruction.cpp
	nvcc dct/gpu_dct.o dct/gpu_quant.o dct/gpu_idct.o reconstruction.cpp -o decoder \
	    $(OPENCV_INCLUDE) $(OPENCV_LIBS) -llz4

dct/gpu_dct.o: dct/gpu_dct.cu dct/gpu_dct.h
	nvcc -c dct/gpu_dct.cu -o dct/gpu_dct.o $(OPENCV_INCLUDE)

dct/gpu_quant.o: dct/gpu_quant.cu dct/gpu_quant.h
	nvcc -c dct/gpu_quant.cu -o dct/gpu_quant.o $(OPENCV_INCLUDE)

dct/gpu_idct.o: dct/gpu_idct.cu dct/gpu_idct.h
	nvcc -c dct/gpu_idct.cu -o dct/gpu_idct.o $(OPENCV_INCLUDE)

client: client.cpp
	g++ -std=c++17 -o client client.cpp

server: server.cpp
	g++ -std=c++17 -o server server.cpp

clean:
	rm -f encoder decoder client server dct/*.o