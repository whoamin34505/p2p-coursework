CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17
TARGET = p2p_node

SRC = src/main.cpp src/network.cpp src/crypto.cpp src/logger.cpp

all:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
