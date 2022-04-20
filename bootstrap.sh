NCPU=$(fgrep 'processor' /proc/cpuinfo | wc -l)

git submodule update --init --recursive

cd third_party/raft
rm -rf build
mkdir build
cd build
cmake ..
make -j $NCPU
make test

# build_tools/bootstrap.sh
cd ../../ccbench/third_party/masstree
./bootstrap.sh
./configure --disable-assertions
make clean
make -j $NCPU CXXFLAGS='-g -W -Wall -O3 -fPIC'
ar cr libkohler_masstree_json.a json.o string.o straccum.o str.o msgpack.o clp.o kvrandom.o compiler.o memdebug.o kvthread.o misc.o
ranlib libkohler_masstree_json.a

# build_tools/bootstrap_mimalloc.sh
cd ../mimalloc
rm -rf out/release
mkdir -p out/release
cd out/release
cmake -DCMAKE_BUILD_TYPE=Release ../..
make clean all -j $NCPU

#cd ../../../../silo
#rm -rf build
#mkdir build
#cd build
#cmake -DCMAKE_BUILD_TYPE=Release ..
#make -j $NCPU
