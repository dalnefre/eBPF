#
# Makefile
#

#CFLAGS = -O2 -Wall -g
#CFLAGS = -O2 -Wall -g -std=c99 -D_GNU_SOURCE
CFLAGS = -O2 -Wall -g -std=c99 -D_GNU_SOURCE
#CFLAGS = -O2 -Wall -march=armv8-a -mtune=cortex-a72
#CFLAGS = -O2 -g
#CFLAGS = -g

TEST =	code \
	bstr \
	json \
	link_test \
	util

EXTRA =	hello_uv

ALL =	client \
	server \
	live \
	ait \
	link \
	link_srv \
	link_cli \
	getmacs \
	$(TEST)

all: $(ALL)

clean:
	rm -f $(ALL) $(EXTRA) *.o

test: $(TEST)
	./code
	./bstr
	./json
	./link_test
	./util

client: client.o proto.o code.o util.o

server: server.o proto.o util.o

live: live.o proto.o json.o bstr.o code.o util.o

ait: ait.o proto.o code.o util.o

link: link.o proto.o code.o util.o ../include/link_ssm.c
	cc $(CFLAGS) -o link link.c proto.o code.o util.o

link_test: link.c proto.o code.o util.o
	cc $(CFLAGS) -DTEST_MAIN -o link_test link.c proto.o code.o util.o

link_cli: link_cli.o proto.o code.o util.o

link_srv: link_srv.o proto.o util.o

code: code.c util.o
	cc $(CFLAGS) -DTEST_MAIN -o code code.c util.o

bstr: bstr.c code.o util.o
	cc $(CFLAGS) -DTEST_MAIN -o bstr bstr.c code.o util.o

json: json.c code.o bstr.o util.o
	cc $(CFLAGS) -DTEST_MAIN -o json json.c code.o bstr.o util.o

util: util.c
	cc $(CFLAGS) -DTEST_MAIN -o util util.c

getmacs: getmacs.c

hello_uv: hello_uv.c
	cc $(CFLAGS) -luv -o hello_uv hello_uv.c

