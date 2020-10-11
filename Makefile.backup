CC=g++
CFLAGS=-I.
DEPS = log.h
OBJ = log.o

%.o: $.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

log: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

test: log.cpp global.cpp
	g++ -o test log.cpp global.cpp
