RAFTD=../third_party/raft

COPTS=-Werror -Werror=return-type -Werror=uninitialized -Wcast-align -fno-omit-frame-pointer -fno-common -fsigned-char -Wunused-variable -g -O2 -fPIC -fprofile-arcs -ftest-coverage -Wno-pointer-sign

CPPOPTS=-Werror -Werror=return-type -Werror=uninitialized -Wcast-align -fno-omit-frame-pointer -fno-common -fsigned-char -Wunused-variable -g -O2 -fPIC #-fprofile-arcs -ftest-coverage

CPPFLAGS=$(CPPOPTS) -I $(RAFTD)/include -I ../include # -I $(RAFTD)/tests
LDLIBS=$(RAFTD)/build/libraft.a -lzmq -lczmq -lmsgpackc -lstdc++

all: t

t.o: t.cc ../include/raft_msgpack.hh ../include/raft_server.hh ../include/raft_client.hh

tags:
	etags ../third_party/ccbench/include/*.hh ../third_party/ccbench/silo/include/*.hh ../third_party/ccbench/silo/*.cc ../third_party/ccbench/common/*.cc ../third_party/raft/src/*.c ../third_party/raft/include/*.h /usr/include/zmq*.h ~/git/czmq/src/*.c ~/git/czmq/include/*.h
