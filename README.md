# simpleserver

A simple C++ server and client applications using TCP sockets. Yeah, I know there isn't much of C++ here...

# make

The amount of threads the server will use can be specified with `NT` argument, e.g.:

```
make [all | server] NT=8
```

Please note that `make` forbids spaces between `NT`, `=` and `8`.

# TODO

+ As integrating OpenSSL with asymmetric cryptography would need too much time for now, add some sort of password authentication like this:
  - The client connects to the server, sending in the user-name (but not password)
  - The server responds by sending out unique random number
  - The client encrypts that random number using the hash of their password as the key
  - The client sends the encrypted random number to the server
  - The server encrypts the random number with the correct hash of the user's password
  - The server compares the two encrypted random numbers
  Source: http://stackoverflow.com/questions/11580944/client-to-server-authentication-in-c-using-sockets
