NT ?= 8

all: server client

server: gsock.o server.o
	g++ gsock.o server.o -o server -pthread -Wall -O2 --std=c++11

server.o: server.cpp
	g++ -c server.cpp -o server.o -D NT=${NT} -Wall -O2 --std=c++11

client: gsock.o client.o
	g++ gsock.o client.o -o client -pthread -Wall -O2 --std=c++11

client.o: client.cpp
	g++ -c client.cpp -o client.o -Wall -O2 --std=c++11

gsock.o: gsock.h gsock.cpp
	g++ -c gsock.cpp -o gsock.o -Wall -O2 --std=c++11

clean:
	rm gsock.o server.o server client.o client
