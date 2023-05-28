.PHONY: all run clean
all:
	gcc -Wall keibot.c `pkg-config fuse3 --cflags --libs` -o kei
run:
	./kei test
clean:
	fusermount -u test