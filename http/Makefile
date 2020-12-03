#
# Makefile
#

CFLAGS = -O2 -Wall -g
#CFLAGS = -O2 -g
#CFLAGS = -g

TEST =	ebpf_test

ALL =	ebpf_map \
	ebpf_fcgi \
	hello_fcgi \
        $(TEST)

all: $(ALL)

clean:
	rm -f $(ALL) *.o

test: $(TEST)
	./ebpf_test

ebpf_map: ebpf_map.c
#	cc $(CFLAGS) -o ebpf_map ebpf_map.c

hello_fcgi: hello_fcgi.c
	cc $(CFLAGS) -lfcgi -o hello_fcgi hello_fcgi.c
#	cc $(CFLAGS) -Wl,-rpath /usr/local/lib -lfcgi -o hello_fcgi hello_fcgi.c
#	cc $(CFLAGS) -o hello_fcgi hello_fcgi.c /usr/local/lib/libfcgi.a

ebpf_fcgi: ebpf_fcgi.c
	cc $(CFLAGS) -lfcgi -lbpf -o ebpf_fcgi ebpf_fcgi.c

ebpf_test: ebpf_fcgi.c
	cc $(CFLAGS) -DTEST_MAIN -lfcgi -lbpf -o ebpf_test ebpf_fcgi.c

install:
	cp -r html/* /var/www/html/

