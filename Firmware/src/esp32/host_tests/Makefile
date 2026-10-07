CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -O1 -g
INCLUDES := -I../include

.PHONY: test clean

test: test_logic
	./test_logic

test_logic: test_logic.cpp ../include/*.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

clean:
	rm -f test_logic
