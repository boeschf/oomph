CC=mpicc
CXX=mpicxx
#PMIX_DIR=/cluster/projects/nn9999k/marcink/software/pmix/2.2.3/
#PMIX_DIR=/cluster/projects/nn9999k/marcink/software/openmpi/master
PMIX_DIR ?= /usr
UCX_DIR=$(HPCX_UCX_DIR)

FLAGS=-O3 -g -DGHEX_DEBUG_LEVEL=0 -std=c++17

all: test

clean:
	rm *.o test

pmi.o: pmi.c pmi.h
	$(CC) $(FLAGS) -I$(PMIX_DIR)/include pmi.c -c

test: ghex_ptr_cb_avail.cpp pmi.o
	$(CXX) $(FLAGS) -I$(UCX_DIR)/include -o $@ $^ -Wl,-rpath -Wl,$(PMIX_DIR)/lib -L$(PMIX_DIR)/lib -lpmix -L$(UCX_DIR)/lib -lucp -Wl,-rpath -Wl,$(UCX_DIR)/lib -Wl,-rpath -Wl,$(PMIX_DIR)/lib -fopenmp

mpitest: mpi_avail_iter.cpp
	$(CXX) $(FLAGS) -I$(UCX_DIR)/include -o $@ $^ -Wl,-rpath -Wl,$(PMIX_DIR)/lib -L$(PMIX_DIR)/lib -lpmix -L$(UCX_DIR)/lib -lucp -Wl,-rpath -Wl,$(UCX_DIR)/lib -Wl,-rpath -Wl,$(PMIX_DIR)/lib -fopenmp

pmixtest: pmixtest.c
	$(CC) $(FLAGS) -I$(UCX_DIR)/include -o $@ $^ -Wl,-rpath -Wl,$(PMIX_DIR)/lib -L$(PMIX_DIR)/lib -lpmix -L$(UCX_DIR)/lib -lucp -Wl,-rpath -Wl,$(UCX_DIR)/lib -Wl,-rpath -Wl,$(PMIX_DIR)/lib -fopenmp

