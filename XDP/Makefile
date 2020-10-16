# Makefile

#CLANG_FLAGS = -target bpf -O2 -Wall -g
CLANG_FLAGS = -target bpf -O2 -fno-jump-tables -Wall -g

ALL =	pass_kern.o \
	drop_kern.o \
	print_kern.o \
	ethertype_kern.o \
	map_array_kern.o \
	multi_kern.o \
	live_kern.o \
	nprocs

all: $(ALL)

clean:
	rm -f $(ALL) *.ll

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

multi_kern.o: multi_kern.c
	clang $(CLANG_FLAGS) -c -o multi_kern.o multi_kern.c

live_kern.o: live_kern.c
	clang $(CLANG_FLAGS) -c -o live_kern.o live_kern.c

nprocs: nprocs.c

