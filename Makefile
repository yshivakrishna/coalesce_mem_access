CXXFLAGS=-std=c++0x -DNUM_COALESCED=1 -DDISTANCE=32
CC=g++
coalesce_mem_access:
%:%.o
%.o:%.c
	$(CC) -c -o $@ $<
