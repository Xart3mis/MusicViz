# build:
# 	gcc -ggdb -c main.c -o ./obj/main.o
# 	gcc -ggdb -I include/ -I include/ffmpeg/ -o main.exe ./obj/main.o -s -Wall -lraylib -lm -lpthread -ldl -lrt -lgmp -lavformat -lavcodec -lavutil -lswresample -lz

# run: build
# 	./main.exe

# default:build
default: build

build:
	gcc -ggdb -I ./include/ -I D:/Documents/raylib/src -c main.c -o ./obj/main.o -Wno-deprecated
	gcc -g  -L ./lib/ ./obj/main.o -o ./out/main.exe -lraylib -lm -lopengl32 -lwinmm -lkernel32 -lgdi32 -lavcodec -lavformat -lswresample -lavutil

run: build
	./out/main.exe

