CC	= /usr/bin/g++
CFLAGS	= -Werror
EXECNAME=omp_race_client
DYNINST_ROOT=../dyninst/dyninst-install

$(EXECNAME): $(EXECNAME).o
	$(CC) $(EXECNAME).o -L../elfutils-0.173/libdw -L$(DYNINST_ROOT)/lib  -ldw -ldyninstAPI -lsymtabAPI -lcommon -ldl -linstructionAPI -std=c++11 -o $(EXECNAME)

$(EXECNAME).o: ./$(EXECNAME).cc
	$(CC) -g -c $(CFLAGS) -std=c++11 -I$(DYNINST_ROOT)/include $(EXECNAME).cc

clean:
	@rm -f $(EXECNAME).o $(EXECNAME) core
