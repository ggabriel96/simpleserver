# simpleserver

A simple C++ server and client applications using TCP sockets. Yeah, I know there isn't much of C++ here...

# make

The amount of threads the server will use can be specified with `NT` argument, e.g.:

```
make [all | server] NT=8
```

Please note that `make` forbids spaces between `NT`, `=` and `8`.

# TODO

- Change the architecture so that `server.cpp` handles accepts incomming connections, adding them to the epoll instance that will be shared to all the threads (insted of the server file descriptor itself)
