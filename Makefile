CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE
TARGET = p2p_node

SRC = src/main.c src/network.c src/crypto.c src/logger.c

LIBS = -pthread -lcrypto

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)