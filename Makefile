NT ?= 2

all: server client

server: gsock.o server.o
	g++ gsock.o server.o -o server -pthread -Wall -O2 --std=c++14

server.o: server.cpp
	g++ -c server.cpp -o server.o -D NT=${NT} -Wall -O2 --std=c++14

client: gsock.o client.o
	g++ gsock.o client.o -o client -pthread -Wall -O2 --std=c++14

client.o: client.cpp
	g++ -c client.cpp -o client.o -Wall -O2 --std=c++14

gsock.o: gsock.h gsock.cpp
	g++ -c gsock.cpp -o gsock.o -Wall -O2 --std=c++14

clean:
	rm gsock.o server.o server client.o client
