# Makefile

#CLANG_FLAGS = -target bpf -O2 -Wall -g
CLANG_FLAGS = -target bpf -O2 -fno-jump-tables -Wall -g

#CFLAGS = -O2 -Wall -g
CFLAGS = -O2 -Wall

OBJS =	pass_kern.o \
	drop_kern.o \
	print_kern.o \
	ethertype_kern.o \
	map_array_kern.o \
	live_kern.o \
	multi_kern.o

TEST =	code

PROGS =	$(TEST) \
	nprocs

ALL =	$(PROGS) $(OBJS)

all: $(ALL)

clean:
	rm -f $(ALL) *.ll

test: $(TEST)
	./code

pass_kern.o: pass_kern.c
	clang $(CLANG_FLAGS) -c -o pass_kern.o pass_kern.c

drop_kern.o: drop_kern.c
	clang $(CLANG_FLAGS) -c -emit-llvm -o drop_kern.ll drop_kern.c
	llc -march=bpf -filetype=obj -o drop_kern.o drop_kern.ll

print_kern.o: print_kern.c
	clang $(CLANG_FLAGS) -c -emit-llvm -o print_kern.ll print_kern.c
	llc -march=bpf -filetype=obj -o print_kern.o print_kern.ll

ethertype_kern.o: ethertype_kern.c
	clang $(CLANG_FLAGS) -c -o ethertype_kern.o ethertype_kern.c

map_array_kern.o: map_array_kern.c
	clang $(CLANG_FLAGS) -c -o map_array_kern.o map_array_kern.c

live_kern.o: live_kern.c ../proto/state.c code.c
	clang $(CLANG_FLAGS) -c -o live_kern.o live_kern.c

multi_kern.o: multi_kern.c
	clang $(CLANG_FLAGS) -c -o multi_kern.o multi_kern.c

code: code.c
	cc $(CFLAGS) -DTEST_MAIN -o code code.c

nprocs: nprocs.c

