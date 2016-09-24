# simpleserver

A simple C++ server and client applications using TCP sockets

Yeah, I know there isn't much of C++ here, it's just C. But I compile with g++ anyways.

# make

The amount of threads the server will use can be specified with `NTHREADS` argument, e.g.:

```
make [all | server] NTHREADS=8
```
