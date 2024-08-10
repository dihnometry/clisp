CC=gcc
CFLAGS=-Wall -lm

ifneq ($(OS),Windows_NT)
	CFLAGS += -ledit
endif

all: clisp.c mpc.c
	gcc -o clisp $^ $(CFLAGS)
