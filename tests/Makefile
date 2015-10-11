
DEBUG = -g -O0

CFLAGS = --std=gnu99 $(DEBUG) -I../

LIBS = ../libncompress.a

all: quick_tests file_tests

quick_tests file_tests : % : %.c $(LIBS)
	$(CC) $(CFLAGS) -o $@ $^
