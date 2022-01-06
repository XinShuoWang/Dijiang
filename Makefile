CXX=g++
TARGET=demo
CXX_FLAGS=-std=c++11 -O3 -g
LIBS=-lpthread -libverbs -lrdmacm
INCLUDE_DIR=./include

all: main.cc
	$(CXX) -o $(TARGET) $^ $(CXX_FLAGS) $(LIBS) -I$(INCLUDE_DIR)

.PHONY: clean
clean:
	rm -rf demo