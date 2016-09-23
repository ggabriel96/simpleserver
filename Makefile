all: server client

server: products.o server.o
	g++ products.o server.o -o server -Wall -O2 --std=c++14

server.o: server.cpp
	g++ -c server.cpp -o server.o -Wall -O2 --std=c++14

client: products.o client.o
	g++ products.o client.o -o client -Wall -O2 --std=c++14

client.o: client.cpp
	g++ -c client.cpp -o client.o -Wall -O2 --std=c++14

products.o: products.h products.cpp
	g++ -c products.cpp -o products.o -Wall -O2 --std=c++14

clean:
	rm products.o server.o server client.o client
