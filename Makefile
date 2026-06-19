# Simple Makefile for the client/server chat.
#
#   make         build both programs into ./bin
#   make clean   remove the build output

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -Iinclude
LDFLAGS = -pthread

BIN_DIR = bin
SRC_DIR = src

all: $(BIN_DIR)/server $(BIN_DIR)/client

$(BIN_DIR)/server: $(SRC_DIR)/server.c include/common.h | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BIN_DIR)/client: $(SRC_DIR)/client.c include/common.h | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)

.PHONY: all clean
