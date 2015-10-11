
DEBUG = -g -O0

CFLAGS = --std=gnu99 $(DEBUG)

LIBS = ../compress42.o

all: quick_tests file_tests

quick_tests file_tests : % : %.c $(LIBS)
	$(CC) $(CFLAGS) -o $@ $^
