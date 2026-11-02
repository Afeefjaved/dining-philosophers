CXX = g++
CXXFLAGS = -std=c++17 -pthread -O2 -Wall -Wextra -Wpedantic

.PHONY: all clean

all: my-dining

my-dining: main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o my-dining

clean:
	rm -f my-dining