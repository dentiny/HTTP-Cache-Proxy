CC = g++
CFLAGS = -std=c++11 -g -pthread

all: proxy

proxy: Proxy.cpp
	$(CC) $(CFLAGS) Proxy.cpp -o proxy

clean:
	rm proxy