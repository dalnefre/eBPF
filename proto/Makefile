#
# Makefile
#

CFLAGS = -O2 -Wall -g
#CFLAGS = -O2 -Wall -march=armv8-a -mtune=cortex-a72
#CFLAGS = -O2 -g
#CFLAGS = -g

TEST =	code \
	json \
	util

ALL =	client \
	server \
	live \
	getmacs \
	$(TEST)

all: $(ALL)

clean:
	rm -f $(ALL) *.o

test: $(TEST)
	./code
	./json
	./util

client: client.o proto.o code.o util.o

server: server.c proto.o util.o

live: live.o proto.o code.o json.o util.o

code: code.c util.o
	cc $(CFLAGS) -DTEST_MAIN -o code code.c util.o

json: json.c code.o util.o
	cc $(CFLAGS) -DTEST_MAIN -o json json.c code.o util.o

util: util.c
	cc $(CFLAGS) -DTEST_MAIN -o util util.c

getmacs: getmacs.c

