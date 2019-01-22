CC	= /usr/bin/g++
BOOST_ROOT=`pwd`/../dyninst/dyninst-build/boost/src/boost
ELFUTIL_ROOT=`pwd`/../dyninst/dyninst-build/elfutils
DYNINST_ROOT=`pwd`/../dyninst/dyninst-install
CFLAGS	= -Werror -I$(DYNINST_ROOT)/include -I$(ELFUTIL_ROOT)/include -std=c++11
EXECNAME=omp_race_client

$(EXECNAME): $(EXECNAME).o
	$(CC) $(EXECNAME).o -L$(DYNINST_ROOT)/lib -L$(ELFUTIL_ROOT)/lib -Wl,-rpath=$(BOOST_ROOT)/stage/lib -Wl,-rpath=$(DYNINST_ROOT)/lib -Wl,-rpath=$(ELFUTIL_ROOT)/lib -ldw -ldyninstAPI -linstructionAPI -lparseAPI -lsymtabAPI -lboost_system -o $(EXECNAME)

$(EXECNAME).o: ./$(EXECNAME).cc
	$(CC) -g -c $(CFLAGS) $(EXECNAME).cc

clean:
	@rm -f $(EXECNAME).o $(EXECNAME) core
