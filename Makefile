all: parsing.c mpc.c mpc.h
	gcc -Wall parsing.c mpc.c -ledit -lm -o parsing

debug: parsing.c 
	gcc -Wall parsing.c mpc.c -g -ledit -lm -o parsing
