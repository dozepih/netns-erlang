# NIF for speeding up sockets

This is a NIF which reimplements the gen_*:socket() systemcall as it was
basically too slow when running with the namespece-support OTP added some
release ago.

Basically, we preallocate a number och sockets in each and every namespace and
will hand these ones out once someone needs them. This will speed things up
considerably.

## Measurements ##


timer:tc(sysSock, sock, [100]).
329µs 100 sockets via a NIF  malloc

100/20 sockets/new_items:
----------------------
connect calls:
  1000st => 445000 445ms    0.45µs/connection
  100st  => 50982  50ms     0.50µs/connection


100/100 sockets/new_items:
----------------------
connect calls:
  1000st => 460980 461ms     0.46µs/connection
  100st  => 52123   52ms     0.52µs/connection


1000/999 sockets:
-----------------
connect calls:
 1000st  => 254259 254ms    0.25µs/connection  no nif alloc involved
  100st  => 31100 311ms     0.31µs/connection

allokera 1000 fd's i nif + en gen_tcp:connect: 0.4ms


netns:
======

[pid 14582] 09:32:26.204530 open("/proc/self/ns/net", O_RDONLY) = 208
[pid 14582] 09:32:26.204870 open("/var/run/netns/topi", O_RDONLY) = 209
[pid 14582] 09:32:26.205192 setns(209, 1073741824) = 0
[pid 14582] 09:32:26.212117 close(209)  = 0
[pid 14582] 09:32:26.212312 socket(PF_INET, SOCK_STREAM, IPPROTO_TCP) = 209
[pid 14582] 09:32:26.212560 setns(208, 1073741824) = 0
[pid 14582] 09:32:26.235994 close(208)  = 0
[pid 14582] 09:32:26.236152 fcntl(209, F_GETFL) = 0x2 (flags O_RDWR)
[pid 14582] 09:32:26.236262 fcntl(209, F_SETFL, O_RDWR|O_NONBLOCK) = 0
[pid 14582] 09:32:26.236396 bind(209, {sa_family=AF_INET, sin_port=htons(0), sin_addr=inet_addr("0.0.0.0")}, 16) = 0
[pid 14582] 09:32:26.236511 getsockname(209, {sa_family=AF_INET, sin_port=htons(40961), sin_addr=inet_addr("0.0.0.0")}, [16]) = 0
[pid 14582] 09:32:26.236609 connect(209, {sa_family=AF_INET, sin_port=htons(80), sin_addr=inet_addr("192.168.0.100")}, 16) = -1 EINPROGRESS (Operation now in progress)

09:37:10.133103 setns
09:37:10.155943 close 22ms
09:37:10.156119 setns
09:37:10.183946 close 27ms
09:38:46.797149 setns
09:38:46.804128 close 6ms
09:38:46.804604 setns
09:38:46.828103 close 24ms

setns takes: 7ms+23ms => 30-50ms

100st no  netns :   22ms 0.22µs/connection
100st via netns : 3305ms   33ms/connection
100st via topins:   83ms 0.83µs/connection
