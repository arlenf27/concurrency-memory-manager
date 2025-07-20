build: my_malloc.c my_malloc.h main.c
	gcc -ansi -pedantic -Wall -o manager my_malloc.c main.c -lpthread
