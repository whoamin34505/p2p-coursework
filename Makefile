CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE
TARGET = p2p_node

SRC = src/main.c src/network.c src/crypto.c src/logger.c

OPENSSL_PREFIX := $(shell brew --prefix openssl 2>/dev/null)

ifneq ($(OPENSSL_PREFIX),)
    CFLAGS += -I$(OPENSSL_PREFIX)/include
    LDFLAGS += -L$(OPENSSL_PREFIX)/lib
endif

LIBS = -pthread -lcrypto

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)