TARGET   := ndsplus
SOURCES  := ndsplus.cpp
INCLUDES ?= `pkg-config libusb-1.0 --cflags`
LIBS      = `pkg-config libusb-1.0 --libs`
CC       ?= gcc
CFLAGS   ?= -Wall -g 
CXX      ?= g++
CXXFLAGS ?= -Wall -g -o $(TARGET)
OBJS      = ndsplus.o

all: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LIBS) -o $(TARGET)

$(OBJS): %.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $(INCLUDES) $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: clean
