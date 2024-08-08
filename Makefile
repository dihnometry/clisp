all: parsing.c mpc.c mpc.h
	gcc -Wall parsing.c mpc.c -ledit -lm -o parsing
