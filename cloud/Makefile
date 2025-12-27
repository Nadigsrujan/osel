CC=gcc
CFLAGS=-I../edge/include -Wall
SRC=src/cloud_scheduler.c ../edge/src/restore_lib.c ../edge/src/checkpoint_lib.c
TARGET=cloud_runtime

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f src/*.o $(TARGET)
