RAFTD=$(HOME)/2022/raft

COPTS=-Werror -Werror=return-type -Werror=uninitialized -Wcast-align -fno-omit-frame-pointer -fno-common -fsigned-char -Wunused-variable -g -O2 -fPIC -fprofile-arcs -ftest-coverage -Wno-pointer-sign

CPPOPTS=-Werror -Werror=return-type -Werror=uninitialized -Wcast-align -fno-omit-frame-pointer -fno-common -fsigned-char -Wunused-variable -g -O2 -fPIC #-fprofile-arcs -ftest-coverage

CPPFLAGS=$(CPPOPTS) -I $(RAFTD)/include # -I $(RAFTD)/tests
LDLIBS=$(RAFTD)/build/libraft.a -lzmq -lczmq -lmsgpackc -lstdc++

all: t

t.o: t.cc raft_msgpack.hh server.hh client.hh

tags:
	etags ../raft/src/*.c ../raft/include/*.h /usr/include/zmq*.h ~/git/czmq/src/*.c ~/git/czmq/include/*.h
