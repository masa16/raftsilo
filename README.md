# RaftSilo

## Install Packages

```
$ sudo apt-get install -y cmake cmake-curses-gui libboost-filesystem-dev libgflags-dev libgoogle-glog-dev libmsgpack-dev
```

## Prepare

```
$ git clone https://github.com/masa16/raftsilo.git
$ cd raftsilo
$ ./bootstrap.sh
```

## Build

```
$ mkdir build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWAL=1 ../src
$ make
```
