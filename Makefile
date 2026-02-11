all: encoder decoder

encoder: dct/gpu_dct.o dct/gpu_quant.o dct/gpu_idct.o main.cpp
	nvcc dct/gpu_dct.o dct/gpu_quant.o dct/gpu_idct.o main.cpp -o encoder -lopencv_core -lopencv_videoio -lopencv_imgproc -lopencv_imgcodecs -llz4

decoder: dct/gpu_dct.o dct/gpu_quant.o dct/gpu_idct.o reconstruction.cpp
	nvcc dct/gpu_dct.o dct/gpu_quant.o dct/gpu_idct.o reconstruction.cpp -o decoder -lopencv_core -lopencv_videoio -lopencv_imgproc -lopencv_imgcodecs -llz4

dct/gpu_dct.o: dct/gpu_dct.cu
	nvcc -c dct/gpu_dct.cu -o dct/gpu_dct.o

dct/gpu_quant.o: dct/gpu_quant.cu
	nvcc -c dct/gpu_quant.cu -o dct/gpu_quant.o

dct/gpu_idct.o: dct/gpu_idct.cu
	nvcc -c dct/gpu_idct.cu -o dct/gpu_idct.o

clean:
	rm -f encoder decoder dct/*.o