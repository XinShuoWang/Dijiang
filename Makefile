CXX=g++
TARGET=demo
CXX_FLAGS=-std=c++11 -O2
LIBS=-lpthread
INCLUDE_DIR=./include

all: main.cc
	$(CXX) -o $(TARGET) $^ $(CXX_FLAGS) $(LIBS) -I$(INCLUDE_DIR)