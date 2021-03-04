#
# Makefile
#

CFLAGS = -O2 -Wall -g
#CFLAGS = -O2 -g
#CFLAGS = -g

TEST =	ebpf_test

ALL =	ebpf_map \
	ebpf_fcgi \
	link_fcgi \
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
	cc $(CFLAGS) -o hello_fcgi hello_fcgi.c -lfcgi
#	cc $(CFLAGS) -o hello_fcgi hello_fcgi.c -Wl,-rpath /usr/local/lib -lfcgi
#	cc $(CFLAGS) -o hello_fcgi hello_fcgi.c /usr/local/lib/libfcgi.a

ebpf_fcgi: ebpf_fcgi.c
	cc $(CFLAGS) -o ebpf_fcgi ebpf_fcgi.c -lfcgi -lbpf
#	cc $(CFLAGS) -DIF_NAME='"eth1"' -o ebpf_fcgi ebpf_fcgi.c -lfcgi -lbpf

ebpf_test: ebpf_fcgi.c
	cc $(CFLAGS) -DTEST_MAIN -o ebpf_test ebpf_fcgi.c -lfcgi -lbpf

link_fcgi: link_fcgi.c
	cc $(CFLAGS) -o link_fcgi link_fcgi.c -lfcgi -lbpf

install:
	cp -r html/* /var/www/html/

