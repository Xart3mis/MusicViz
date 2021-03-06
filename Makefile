build:
	cc -ggdb -c main.c -o ./obj/main.o
	cc -ggdb -I include/ -I include/ffmpeg/ -o main.bin ./obj/main.o -s -Wall -lraylib -lm -lpthread -ldl -lrt -lgmp -lavformat -lavcodec -lavutil -lswresample -lz

run: build
	./main.bin

default:run
