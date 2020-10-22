# Protocol Lab

A set of experimental tools for exploring communication protocols on Linux.

To compile these tools for your system and run unit tests:

```
$ make clean all test
```

## Client/Server Examples

The `client` and `server` programs are userspace applications that send and receive simple messages over sockets.
Common command-line processing (implemented in [`proto.c`](proto.c)) recognizes keyword-based arguments
that set options in the global `proto_opt` structure.
Keyword options include:

  * `.family` = `AF_INET` (default), `AF_PACKET`
  * `.sock_type` = `SOCK_DGRAM` (default), `SOCK_RAW`, `SOCK_STREAM`
  * `.eth_proto` = `ETH_P_IP` (default), `ETH_P_ALL`
  * `.ip_proto` = `IPPROTO_DEFAULT` (default), `IPPROTO_UDP`, `IPPROTO_TCP`, `IPPROTO_RAW`
  * `.ip_addr` = `INADDR_LOOPBACK` (default), `INADDR_ANY`, `INADDR_BROADCAST`
  * `.filter` = `FILTER_NONE` (default), `FILTER_IP`, `FILTER_IPV6`, `FILTER_ARP`

Network interfaces (`.if_index`, default =`0`) can be specified by name (e.g.: `if=eth0`) or number (e.g.: `if=2`).
A list of network interfaces is available through the following command-line program:

```
$ ip link list
```

An IP host address (`.ip_addr`, default = `127.0.0.1`) and port (`.ip_port`, default =`8888`)
can be specified with dot/colon notation (e.g.: `127.0.0.1:8888`).
Both parts are optional, so you can specify just a host (e.g.: `127.0.0.1`), or just a port (e.g.: `:8888`).
A list of interface addresses is available through the following command-line program:

```
$ ifconfig -a
```

Shortcut keywords are available to set multiple related options:

  * `UDP` = `AF_INET SOCK_DGRAM IPPROTO_UDP`
  * `TCP` = `AF_INET SOCK_STREAM IPPROTO_TCP`
  * `ETH` = `AF_PACKET SOCK_RAW ETH_P_ALL`
  * `IP` = `AF_INET ETH_P_IP`

When a lab program is run, it normalizes recognized command-line arguments and prints the result for reference.

### `client` Program

The `client` program opens a socket, constructs a sample message, attempts to send it to the specified interface/address, and exits.
The sample message payload is:

```
0000:  0a 8e 48 65 6c 6c 6f 2c  20 57 6f 72 6c 64 21 0a  |..Hello, World!.|
```

### `server` Program

The `server` program opens a socket, binds it to the specified interface/address, and receives messages in a loop.
A hexdump of each message is printed to `stdout`.

### `live` Program

The `live` program is a client/server implementing a basic link-liveness protocol.
It sets default options `AF_PACKET SOCK_RAW ETH_P_DALE` before processing command-line arguments.
It opens a socket, binds it, sends an initialization message, and waits for messages in a loop.
It responds to each link-liveness message it receives, up to a message-count limit (currently 5).
Note that opening an `AF_PACKET` socket requires root permission, so run the program like this:

```
$ sudo ./live if=eth0
```
