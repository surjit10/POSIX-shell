
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -g


TARGET = myshell


SOURCES = main.cpp pipeline.cpp shell_signal.cpp

OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)

.PHONY: all clean test

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS)


%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)

test: $(TARGET)
	./tests/smoke_test.sh
