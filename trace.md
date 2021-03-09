# Execution Traces

## A=link_user/link_kern, B=link_fcgi/link_kern

### Betty

```
$ sudo cat /sys/kernel/debug/tracing/trace_pipe
           <...>-5071  [000] ..s. 3378959.089444: 0: outbound_AIT: setting LF_SEND + LF_FULL
           <...>-5071  [000] ..s. 3378959.089461: 0: outbound_AIT (44 octets)
           <...>-5071  [000] ..s. 3378959.089466: 0: [44] 88141ffffffffff ffffffffffffffff
           <...>-16888 [000] ..s. 3378959.089651: 0: on_frame_recv: (Got_AIT, Ack_AIT) len=44
           <...>-16835 [000] ..s. 3378959.089845: 0: on_frame_recv: (Ack_Ack, Proceed) len=44
            sshd-16835 [000] ..s. 3378959.089855: 0: clear_AIT: setting !LF_SEND
            sshd-16835 [000] ..s. 3378959.089914: 0: clear_AIT: outbound VALID still set!
           nginx-5071  [000] ..s. 3378977.800926: 0: on_frame_recv: setting !LF_FULL
           nginx-5071  [000] ..s. 3378979.808480: 0: outbound_AIT: setting LF_SEND + LF_FULL
           nginx-5071  [000] ..s. 3378979.808498: 0: outbound_AIT (44 octets)
           nginx-5071  [000] ..s. 3378979.808503: 0: [44] 88149ffffffffff ffffffffffffffff
       link_fcgi-16888 [000] ..s. 3378979.808686: 0: on_frame_recv: (Got_AIT, Ack_AIT) len=44
           nginx-5071  [000] .ns. 3378979.808869: 0: on_frame_recv: (Ack_Ack, Proceed) len=44
           nginx-5071  [000] .ns. 3378979.808877: 0: clear_AIT: setting !LF_SEND
           nginx-5071  [000] .ns. 3378979.808880: 0: clear_AIT: outbound VALID still set!
           nginx-5071  [000] ..s. 3378981.806033: 0: on_frame_recv: setting !LF_FULL
       link_fcgi-16888 [000] ..s. 3378983.810656: 0: outbound_AIT: setting LF_SEND + LF_FULL
       link_fcgi-16888 [000] ..s. 3378983.810673: 0: outbound_AIT (44 octets)
       link_fcgi-16888 [000] ..s. 3378983.810677: 0: [44] 88154ffffffffff ffffffffffffffff
       link_fcgi-16888 [000] ..s. 3378983.810864: 0: on_frame_recv: (Got_AIT, Ack_AIT) len=44
           nginx-5071  [000] ..s1 3378983.811063: 0: on_frame_recv: (Ack_Ack, Proceed) len=44
           nginx-5071  [000] ..s1 3378983.811066: 0: clear_AIT: setting !LF_SEND
           nginx-5071  [000] ..s1 3378983.811069: 0: clear_AIT: outbound VALID still set!
           nginx-5071  [000] ..s. 3378985.807118: 0: on_frame_recv: setting !LF_FULL
```

### Archie

```
$ sudo XDP/link_user eth0
LINK_STATE [2]
outbound[44] =
0000:  08 8c 41 6d 20 49 20 41  6c 69 63 65 3f 0a 00 ff  |..Am I Alice?...|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
user_flags = 0x00000000 (-s-e)
inbound[44] =
0000:  08 82 3f 0a 00 ff ff ff  ff ff ff ff ff ff ff ff  |..?.............|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
link_flags = 0x00000005 (----e&-A)
frame[64] =
0000:  dc a6 32 67 7e a7 b8 27  eb f3 5a d2 da 1e 8a 80  |..2g~..'..Z.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff 00 00 00 00  |................|
i,u = (1,2)
len = 0
seq = 0x7afb5419
monitor pid=5801
reader pid=5802
writer pid=5803
inbound FULL set.
inbound AIT:
0000:  08 81 41 ff ff ff ff ff  ff ff ff ff ff ff ff ff  |..A.............|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
inbound FULL cleared.
inbound FULL set.
inbound AIT:
0000:  08 81 49 ff ff ff ff ff  ff ff ff ff ff ff ff ff  |..I.............|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
inbound FULL cleared.
inbound FULL set.
inbound AIT:
0000:  08 81 54 ff ff ff ff ff  ff ff ff ff ff ff ff ff  |..T.............|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
inbound FULL cleared.
^Cparent exit.

$ sudo cat /sys/kernel/debug/tracing/trace_pipe
          <idle>-0     [000] ..s. 3378928.964093: 0: on_frame_recv: (Pong, Got_AIT) len=44
          <idle>-0     [000] ..s. 3378928.964104: 0: inbound_AIT (44 octets)
          <idle>-0     [000] ..s. 3378928.964106: 0: inbound_AIT: setting LF_RECV
          <idle>-0     [000] ..s. 3378928.964277: 0: on_frame_recv: (Ack_AIT, Ack_Ack) len=44
          <idle>-0     [000] ..s. 3378928.964279: 0: release_AIT: setting !LF_RECV + LF_VALD
          <idle>-0     [000] ..s. 3378928.964283: 0: release_AIT (44 octets)
          <idle>-0     [000] ..s. 3378928.964287: 0: [44] 88141ffffffffff ffffffffffffffff
          <idle>-0     [000] ..s. 3378928.964882: 0: on_frame_recv: setting !LF_VALD
          <idle>-0     [000] ..s. 3378949.682879: 0: on_frame_recv: (Pong, Got_AIT) len=44
          <idle>-0     [000] ..s. 3378949.682886: 0: inbound_AIT (44 octets)
          <idle>-0     [000] ..s. 3378949.682889: 0: inbound_AIT: setting LF_RECV
          <idle>-0     [000] ..s. 3378949.683064: 0: on_frame_recv: (Ack_AIT, Ack_Ack) len=44
          <idle>-0     [000] ..s. 3378949.683066: 0: release_AIT: setting !LF_RECV + LF_VALD
          <idle>-0     [000] ..s. 3378949.683070: 0: release_AIT (44 octets)
          <idle>-0     [000] ..s. 3378949.683074: 0: [44] 88149ffffffffff ffffffffffffffff
          <idle>-0     [000] ..s. 3378949.683938: 0: on_frame_recv: setting !LF_VALD
          <idle>-0     [000] ..s. 3378953.685006: 0: on_frame_recv: (Pong, Got_AIT) len=44
          <idle>-0     [000] ..s. 3378953.685012: 0: inbound_AIT (44 octets)
          <idle>-0     [000] ..s. 3378953.685014: 0: inbound_AIT: setting LF_RECV
          <idle>-0     [000] ..s. 3378953.685189: 0: on_frame_recv: (Ack_AIT, Ack_Ack) len=44
          <idle>-0     [000] ..s. 3378953.685191: 0: release_AIT: setting !LF_RECV + LF_VALD
          <idle>-0     [000] ..s. 3378953.685195: 0: release_AIT (44 octets)
          <idle>-0     [000] ..s. 3378953.685199: 0: [44] 88154ffffffffff ffffffffffffffff
          <idle>-0     [000] ..s. 3378953.695035: 0: on_frame_recv: setting !LF_VALD
```


## A=XDP, B=proto

### Alice

```
$ make link_kern.o  # LOG_LEVEL=3
$ sudo ip -force link set dev eth0 xdp obj link_kern.o
$ sudo cat /sys/kernel/debug/tracing/trace_pipe

<idle>-0     [000] ..s. 2686081.469075: 0: [60] ffffffffffffdca6 32677ea7da1e8080
<idle>-0     [000] ..s. 2686081.469103: 0:   (0,0) <--
<idle>-0     [000] ..s. 2686081.469108: 0: len = 0
<idle>-0     [000] ..s. 2686081.469114: 0: Init: dst mac is bcast
<idle>-0     [000] ..s. 2686081.469119: 0: outbound FULL cleared.
<idle>-0     [000] ..s. 2686081.469127: 0:   (0,0) #1 -->
<idle>-0     [000] ..s. 2686081.469133: 0: recv: proto=0xda1e len=60 rc=3
<idle>-0     [000] ..s. 2686081.486239: 0: [60] b827ebf35ad2dca6 32677ea7da1e8180
<idle>-0     [000] ..s. 2686081.486261: 0:   (0,1) <--
<idle>-0     [000] ..s. 2686081.486265: 0: len = 0
<idle>-0     [000] ..s. 2686081.486271: 0: ENTL set on recv
<idle>-0     [000] ..s. 2686081.486274: 0: Alice sending initial Pong
<idle>-0     [000] ..s. 2686081.486278: 0: outbound FULL cleared.
<idle>-0     [000] ..s. 2686081.486291: 0:   (1,2) #2 -->
<idle>-0     [000] ..s. 2686081.486297: 0: recv: proto=0xda1e len=60 rc=3
<idle>-0     [000] ..s. 2686081.495703: 0: [60] b827ebf35ad2dca6 32677ea7da1e9180
<idle>-0     [000] ..s. 2686081.495720: 0:   (2,1) <--
<idle>-0     [000] ..s. 2686081.495724: 0: len = 0
<idle>-0     [000] ..s. 2686081.495731: 0: outbound FULL cleared.
<idle>-0     [000] ..s. 2686081.495738: 0:   (1,2) #3 -->
<idle>-0     [000] ..s. 2686081.495749: 0: recv: proto=0xda1e len=60 rc=3
<idle>-0     [000] ..s. 2686081.502648: 0: [60] b827ebf35ad2dca6 32677ea7da1e9180
<idle>-0     [000] ..s. 2686081.502665: 0:   (2,1) <--
<idle>-0     [000] ..s. 2686081.502669: 0: len = 0
<idle>-0     [000] ..s. 2686081.502676: 0: outbound FULL cleared.
<idle>-0     [000] ..s. 2686081.502683: 0:   (1,2) #4 -->
<idle>-0     [000] ..s. 2686081.502689: 0: recv: proto=0xda1e len=60 rc=3
<idle>-0     [000] ..s. 2686081.510149: 0: [60] b827ebf35ad2dca6 32677ea7da1e9180
<idle>-0     [000] ..s. 2686081.510166: 0:   (2,1) <--
<idle>-0     [000] ..s. 2686081.510171: 0: len = 0
<idle>-0     [000] ..s. 2686081.510177: 0: outbound FULL cleared.
<idle>-0     [000] ..s. 2686081.510185: 0:   (1,2) #5 -->

<idle>-0     [000] ..s. 2686081.510196: 0: recv: proto=0xda1e len=60 rc=3
<idle>-0     [000] ..s. 2686081.518070: 0: [60] b827ebf35ad2dca6 32677ea7da1e9180
<idle>-0     [000] ..s. 2686081.518087: 0:   (2,1) <--
<idle>-0     [000] ..s. 2686081.518091: 0: len = 0
<idle>-0     [000] ..s. 2686081.518097: 0: outbound FULL cleared.
<idle>-0     [000] ..s. 2686081.518105: 0:   (1,2) #6 -->
<idle>-0     [000] ..s. 2686081.518116: 0: recv: proto=0xda1e len=60 rc=3
<idle>-0     [000] ..s. 2686081.525515: 0: [60] b827ebf35ad2dca6 32677ea7da1e9180
<idle>-0     [000] ..s. 2686081.525531: 0:   (2,1) <--
<idle>-0     [000] ..s. 2686081.525535: 0: len = 0
<idle>-0     [000] ..s. 2686081.525542: 0: outbound FULL cleared.
<idle>-0     [000] ..s. 2686081.525549: 0:   (1,2) #7 -->
<idle>-0     [000] ..s. 2686081.525561: 0: recv: proto=0xda1e len=60 rc=3
<idle>-0     [000] ..s. 2686081.533385: 0: [60] b827ebf35ad2dca6 32677ea7da1e9180
<idle>-0     [000] ..s. 2686081.533401: 0:   (2,1) <--
<idle>-0     [000] ..s. 2686081.533405: 0: len = 0
<idle>-0     [000] ..s. 2686081.533411: 0: outbound FULL cleared.
<idle>-0     [000] ..s. 2686081.533418: 0:   (1,2) #8 -->
<idle>-0     [000] ..s. 2686081.533427: 0: recv: proto=0xda1e len=60 rc=3
<idle>-0     [000] ..s. 2686081.541209: 0: [60] b827ebf35ad2dca6 32677ea7da1e9180
<idle>-0     [000] ..s. 2686081.541225: 0:   (2,1) <--
<idle>-0     [000] ..s. 2686081.541229: 0: len = 0
<idle>-0     [000] ..s. 2686081.541235: 0: outbound FULL cleared.
<idle>-0     [000] ..s. 2686081.541243: 0:   (1,2) #9 -->
<idle>-0     [000] ..s. 2686081.541254: 0: recv: proto=0xda1e len=60 rc=3
```

### Bob

```
$ sudo ./link if=eth0 log=3

./link AF_PACKET SOCK_RAW ETH_P_DALE if=2 log=3
CLOCK_REALTIME resolution 0.000000001
eth_local = dc:a6:32:67:7e:a7
link status = 1
1614620675.472672016 Message[60] --> 
0000:  ff ff ff ff ff ff dc a6  32 67 7e a7 da 1e 80 80  |........2g~.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
  (0,0) #0 -->
1614620675.480799646 Message[60] <-- 
0000:  dc a6 32 67 7e a7 b8 27  eb f3 5a d2 da 1e 80 80  |..2g~..'..Z.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
  (0,0) <--
dst = dc:a6:32:67:7e:a7
src = b8:27:eb:f3:5a:d2
len = 0
cmp(dst, src) = -43
ENTL set on send
Bob sending initial Ping
eth_remote = b8:27:eb:f3:5a:d2
  (0,1) #1 -->
recv: proto=0xda1e len=60 rc=3
1614620675.489879487 Message[60] --> 
0000:  b8 27 eb f3 5a d2 dc a6  32 67 7e a7 da 1e 81 80  |.'..Z...2g~.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
1614620675.495449939 Message[60] <-- 
0000:  dc a6 32 67 7e a7 b8 27  eb f3 5a d2 da 1e 8a 80  |..2g~..'..Z.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
  (1,2) <--
dst = dc:a6:32:67:7e:a7
src = b8:27:eb:f3:5a:d2
len = 0
  (2,1) #2 -->
recv: proto=0xda1e len=60 rc=3
1614620675.499362148 Message[60] --> 
0000:  b8 27 eb f3 5a d2 dc a6  32 67 7e a7 da 1e 91 80  |.'..Z...2g~.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
1614620675.502939904 Message[60] <-- 
0000:  dc a6 32 67 7e a7 b8 27  eb f3 5a d2 da 1e 8a 80  |..2g~..'..Z.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
  (1,2) <--
dst = dc:a6:32:67:7e:a7
src = b8:27:eb:f3:5a:d2
len = 0
  (2,1) #3 -->
recv: proto=0xda1e len=60 rc=3
1614620675.506313649 Message[60] --> 
0000:  b8 27 eb f3 5a d2 dc a6  32 67 7e a7 da 1e 91 80  |.'..Z...2g~.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
1614620675.509887294 Message[60] <-- 
0000:  dc a6 32 67 7e a7 b8 27  eb f3 5a d2 da 1e 8a 80  |..2g~..'..Z.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
  (1,2) <--
dst = dc:a6:32:67:7e:a7
src = b8:27:eb:f3:5a:d2
len = 0
  (2,1) #4 -->
recv: proto=0xda1e len=60 rc=3
1614620675.513815392 Message[60] --> 
0000:  b8 27 eb f3 5a d2 dc a6  32 67 7e a7 da 1e 91 80  |.'..Z...2g~.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
1614620675.517647677 Message[60] <-- 
0000:  dc a6 32 67 7e a7 b8 27  eb f3 5a d2 da 1e 8a 80  |..2g~..'..Z.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
  (1,2) <--
dst = dc:a6:32:67:7e:a7
src = b8:27:eb:f3:5a:d2
len = 0
  (2,1) #5 -->
recv: proto=0xda1e len=60 rc=3
1614620675.521732991 Message[60] --> 
0000:  b8 27 eb f3 5a d2 dc a6  32 67 7e a7 da 1e 91 80  |.'..Z...2g~.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
1614620675.525220084 Message[60] <-- 
0000:  dc a6 32 67 7e a7 b8 27  eb f3 5a d2 da 1e 8a 80  |..2g~..'..Z.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
  (1,2) <--
dst = dc:a6:32:67:7e:a7
src = b8:27:eb:f3:5a:d2
len = 0
  (2,1) #6 -->
recv: proto=0xda1e len=60 rc=3
1614620675.529186458 Message[60] --> 
0000:  b8 27 eb f3 5a d2 dc a6  32 67 7e a7 da 1e 91 80  |.'..Z...2g~.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
1614620675.532864100 Message[60] <-- 
0000:  dc a6 32 67 7e a7 b8 27  eb f3 5a d2 da 1e 8a 80  |..2g~..'..Z.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
  (1,2) <--
dst = dc:a6:32:67:7e:a7
src = b8:27:eb:f3:5a:d2
len = 0
  (2,1) #7 -->
recv: proto=0xda1e len=60 rc=3
1614620675.537062059 Message[60] --> 
0000:  b8 27 eb f3 5a d2 dc a6  32 67 7e a7 da 1e 91 80  |.'..Z...2g~.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
1614620675.540759423 Message[60] <-- 
0000:  dc a6 32 67 7e a7 b8 27  eb f3 5a d2 da 1e 8a 80  |..2g~..'..Z.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
  (1,2) <--
dst = dc:a6:32:67:7e:a7
src = b8:27:eb:f3:5a:d2
len = 0
  (2,1) #8 -->
recv: proto=0xda1e len=60 rc=3
1614620675.544885199 Message[60] --> 
0000:  b8 27 eb f3 5a d2 dc a6  32 67 7e a7 da 1e 91 80  |.'..Z...2g~.....|
0010:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0020:  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
0030:  ff ff ff ff ff ff ff ff  ff ff ff ff              |............    |
frame 8 exceeded limit
```

## Basic AIT

### Steady State

In the steady state,
the endpoints are exchanging
empty frames (_Ping_/_Pong_).
All `VALD`/`FULL` flags are `false`.

```javascript
{
  "outbound":"...",
  "user_flags":{ ..., "VALD":false, "FULL":false },
  "inbound":"...",
  "link_flags":{ ..., "VALD":false, "FULL":false, ... },
  "frame":[ ..., 218, 30, 138, 128, ... ],
  "i":1, "u":2, "len":0,
  "seq":1002
}
```

### Outbound AIT

When the link is ready for more data
(i.e.: `(link_flags.FULL == false) && (user_flags.VALD == false)`),
and the user has data to send,
it fills the `outbound` buffer
and sets `user_flags.VALD = true`.

```javascript
{
  "outbound":"\u0008\u0083Hi\n...",
  "user_flags":{ ..., "VALD":true, ... },
  "inbound":"...",
  "link_flags":{ ..., "FULL":false, ... },
  ...
}
```

When the sending endpoint receives an empty frame
and `user_flags.VALD == true`,
it sets `link_flags.FULL = true`
and starts the AIT handshake with _GotAIT_.

```javascript
{
  "outbound":"\u0008\u0083Hi\n...",
  "user_flags":{ ..., "VALD":true, ... },
  "inbound":"...",
  "link_flags":{ ..., "FULL":true, ... },
  "frame":[ ... ],
  "i":1, "u":3, "len":>0,
  "seq":1003
}
```

After the AIT handshake (_GotAIT_, _AckAIT_, _AckAck_),
if the receiving endpoint has room
(i.e.: `(user_flags.FULL == false) && (link_flags.VALD == false)`),
if fills the `inbound` buffer
and sets `link_flags.VALD = true`,
completing the AIT handshake with _Proceed_.

```javascript
{
  "outbound":"...",
  "user_flags":{ ..., "FULL":false },
  "inbound":"\u0008\u0083Hi\n...",
  "link_flags":{ ..., "VALD":true, ... },
  "frame":[ ... ],
  "i":5, "u":6, "len":>0,
  "seq":1005
}
```

When the sending endpoint receives _Proceed_,
the AIT transfer is complete.
If `user_flags.VALD == false`,
then it sets `link_flags.FULL = false`.
Otherwise we note that the transfer is complete,
and wait for the user to set `user_flags.VALD = false`.

```javascript
{
  "outbound":"...",
  "user_flags":{ ..., "VALD":false, ... },
  "inbound":"...",
  "link_flags":{ ..., "FULL":false, ... },
  "frame":[ ... ],
  "i":6, "u":2, "len":0,
  "seq":1007
}
```

When the receiving user sees `link_flags.VALD == true`,
it copies the data from `inbound`
and sets `user_flags.FULL = true`.

```javascript
{
  "outbound":"...",
  "user_flags":{ ..., "FULL":true },
  "inbound":"\u0008\u0083Hi\n...",
  "link_flags":{ ..., "VALD":true, ... },
  ...
}
```

When the receiving endpoint sees
the user has the data
(i.e.: `(user_flags.FULL == true) && (link_flags.VALD == true)`),
it sets `link_flags.VALD = false`.
