CC = gcc
CXX = g++
CPPFLAGS += -Iinclude
CFLAGS += -pthread
CXXFLAGS += -std=c++17 -pthread

BUILD_DIR = build
OBJECT_DIR = $(BUILD_DIR)/obj

.PHONY: all clean server client

all: $(BUILD_DIR)/server $(BUILD_DIR)/client

server: $(BUILD_DIR)/server

client: $(BUILD_DIR)/client

$(OBJECT_DIR):
	mkdir -p $@

$(BUILD_DIR)/server: $(OBJECT_DIR)/server.o $(OBJECT_DIR)/socket_utils.o $(OBJECT_DIR)/data_structs.o
	$(CC) $(LDFLAGS) $^ -o $@ $(CFLAGS) -pthread

$(BUILD_DIR)/client: $(OBJECT_DIR)/client.o $(OBJECT_DIR)/socket_utils.o $(OBJECT_DIR)/crypto_utils.o
	$(CXX) $(LDFLAGS) $^ -o $@ -pthread -lsfml-graphics -lsfml-window -lsfml-system

$(OBJECT_DIR)/server.o: src/server.c include/data_structs.h include/socket_utils.h include/protocol.h | $(OBJECT_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJECT_DIR)/socket_utils.o: src/socket_utils.c include/socket_utils.h | $(OBJECT_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJECT_DIR)/data_structs.o: src/data_structs.c include/data_structs.h include/protocol.h | $(OBJECT_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJECT_DIR)/crypto_utils.o: src/crypto_utils.c include/crypto_utils.h | $(OBJECT_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJECT_DIR)/client.o: src/client.cpp include/socket_utils.h include/crypto_utils.h include/protocol.h | $(OBJECT_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
