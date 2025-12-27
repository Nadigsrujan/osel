CC=gcc
CFLAGS=-Iinclude -Wall
SRC=src/edge_scheduler.c src/checkpoint_lib.c src/restore_lib.c src/telemetry_collector.c src/secure_channel.c
OBJ=$(SRC:.c=.o)
TARGET=edge_runtime

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f src/*.o $(TARGET)
