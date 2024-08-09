all: clisp.c
	gcc -Wall clisp.c mpc.c -ledit -lm -o clisp

debug: clisp.c 
	gcc -Wall clisp.c mpc.c -g -ledit -lm -o clisp
