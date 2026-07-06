CC=gcc
CFLAGS=-Wall -lm

ifneq ($(OS),Windows_NT)
	CFLAGS += -ledit
endif

all: clisp.c mpc.c
	$(CC) -o clisp $^ $(CFLAGS)

debug: clisp.c mpc.c
	$(CC) -g -fsanitize=address -o clisp $^ $(CFLAGS)
