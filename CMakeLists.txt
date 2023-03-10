cmake_minimum_required(VERSION 3.10)

project(ccbench_silo
        VERSION 0.0.1
        DESCRIPTION "silo of ccbench"
        LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/third_party/ccbench/cmake)

option(ENABLE_SANITIZER "enable sanitizer on debug build" ON)
option(ENABLE_UB_SANITIZER "enable undefined behavior sanitizer on debug build" OFF)
option(ENABLE_COVERAGE "enable coverage on debug build" OFF)

find_package(Doxygen)
find_package(Threads REQUIRED)
find_package(gflags REQUIRED)
find_package(glog REQUIRED)
find_package(Boost COMPONENTS filesystem)
find_package(msgpack)

## load in pkg-config support
find_package(PkgConfig)

## use pkg-config to get hints for 0mq locations
pkg_check_modules(PC_ZeroMQ QUIET zmq)
## use the hint from above to find where 'zmq.hpp' is located
find_path(ZeroMQ_INCLUDE_DIR
  NAMES zmq.h
  PATHS ${PC_ZeroMQ_INCLUDE_DIRS}
  )
## use the hint from about to find the location of libzmq
find_library(ZeroMQ_LIBRARY
  NAMES zmq
  PATHS ${PC_ZeroMQ_LIBRARY_DIRS}
  )

## use pkg-config to get hints for 0mq locations
pkg_check_modules(PC_CZeroMQ QUIET czmq)
## use the hint from above to find where 'zmq.hpp' is located
find_path(CZeroMQ_INCLUDE_DIR
  NAMES czmq.h
  PATHS ${PC_CZeroMQ_INCLUDE_DIRS}
  )
## use the hint from about to find the location of libczmq
find_library(CZeroMQ_LIBRARY
  NAMES czmq
  PATHS ${PC_CZeroMQ_LIBRARY_DIRS}
  )

## add the include directory to our compile directives
#target_include_directories(test2 PUBLIC ${ZeroMQ_INCLUDE_DIR})
## at the 0mq library to our link directive
#target_link_libraries(test2 PUBLIC ${ZeroMQ_LIBRARY})

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CompileOptions)

if (DEFINED DURABLE_EPOCH)
    add_definitions(-DDURABLE_EPOCH=${DURABLE_EPOCH})
else ()
    add_definitions(-DDURABLE_EPOCH=1)
    set(DURABLE_EPOCH 1)
endif ()

file(GLOB FOLLOWER_SOURCES
  src/raft_follower.cc
  )

if (${DURABLE_EPOCH} EQUAL 0)
  file(GLOB SILO_SOURCES
    #src/raft_server.cc
    src/raft_cc.cc
    src/raft_txn.cc
    src/raft_silo.cc
    third_party/ccbench/common/result.cc
    third_party/ccbench/common/util.cc
    third_party/ccbench/silo/util.cc
    third_party/ccbench/silo/transaction.cc
    third_party/ccbench/silo/result.cc
    )
else ()
  file(GLOB SILO_SOURCES
    #src/raft_server.cc
    src/raft_cc.cc
    src/raft_txn.cc
    src/raft_silo.cc
    third_party/ccbench/common/result.cc
    third_party/ccbench/common/util.cc
    third_party/ccbench/silo/util.cc
    third_party/ccbench/silo/transaction.cc
    third_party/ccbench/silo_logging/silo_result.cc
    third_party/ccbench/silo_logging/transaction_lg.cc
    third_party/ccbench/silo_logging/util_lg.cc
    third_party/ccbench/silo_logging/log_buffer.cc
    third_party/ccbench/silo_logging/logger.cc
    third_party/ccbench/silo_logging/notifier.cc
    third_party/ccbench/silo_logging/result_lg.cc
    )
endif ()

add_executable(raft_silo ${SILO_SOURCES})
add_executable(raft_follower ${FOLLOWER_SOURCES})

target_include_directories(raft_silo
  PUBLIC include
  PUBLIC third_party/raft/include
  PUBLIC third_party/ccbench/silo_logging
  PUBLIC third_party/ccbench/silo
  #PUBLIC third_party/ccbench/include
  PUBLIC ${ZeroMQ_INCLUDE_DIR}
  PUBLIC ${CZeroMQ_INCLUDE_DIR}
  )

target_include_directories(raft_follower
  PUBLIC include
  PUBLIC ${ZeroMQ_INCLUDE_DIR}
  )

target_link_libraries(raft_silo
  PUBLIC Boost::filesystem
  PUBLIC gflags::gflags
  ${PROJECT_SOURCE_DIR}/third_party/ccbench/third_party/mimalloc/out/release/libmimalloc.a
  ${PROJECT_SOURCE_DIR}/third_party/ccbench/third_party/masstree/libkohler_masstree_json.a
  ${PROJECT_SOURCE_DIR}/third_party/raft/build/libraft.a
  PUBLIC Threads::Threads
  PUBLIC msgpackc
  PUBLIC ${ZeroMQ_LIBRARY}
  PUBLIC ${CZeroMQ_LIBRARY}
  PUBLIC numa
  #PUBLIC pmem
  )

target_link_libraries(raft_follower
  PUBLIC ${ZeroMQ_LIBRARY}
  )

if (DEFINED ADD_ANALYSIS)
  add_definitions(-DADD_ANALYSIS=${ADD_ANALYSIS})
else ()
  add_definitions(-DADD_ANALYSIS=0)
endif ()

if (DEFINED BACK_OFF)
  add_definitions(-DBACK_OFF=${BACK_OFF})
else ()
  add_definitions(-DBACK_OFF=0)
endif ()

if (DEFINED KEY_SIZE)
  add_definitions(-DKEY_SIZE=${KEY_SIZE})
else ()
  add_definitions(-DKEY_SIZE=8)
endif ()

if (DEFINED MASSTREE_USE)
  add_definitions(-DMASSTREE_USE=${MASSTREE_USE})
else ()
  add_definitions(-DMASSTREE_USE=1)
endif ()

if (DEFINED NO_WAIT_LOCKING_IN_VALIDATION)
  add_definitions(-DNO_WAIT_LOCKING_IN_VALIDATION=${NO_WAIT_LOCKING_IN_VALIDATION})
else ()
  add_definitions(-DNO_WAIT_LOCKING_IN_VALIDATION=1)
endif ()

if (DEFINED NO_WAIT_OF_TICTOC)
  add_definitions(-DNO_WAIT_OF_TICTOC=${NO_WAIT_OF_TICTOC})
else ()
  add_definitions(-DNO_WAIT_OF_TICTOC=0)
endif ()

if (DEFINED PARTITION_TABLE)
  add_definitions(-DPARTITION_TABLE=${PARTITION_TABLE})
else ()
  add_definitions(-DPARTITION_TABLE=0)
endif ()

if (DEFINED PROCEDURE_SORT)
  add_definitions(-DPROCEDURE_SORT=${PROCEDURE_SORT})
else ()
  add_definitions(-DPROCEDURE_SORT=0)
endif ()

if (DEFINED SLEEP_READ_PHASE)
  add_definitions(-DSLEEP_READ_PHASE=${SLEEP_READ_PHASE})
else ()
  add_definitions(-DSLEEP_READ_PHASE=0)
endif ()

if (DEFINED VAL_SIZE)
  add_definitions(-DVAL_SIZE=${VAL_SIZE})
else ()
  add_definitions(-DVAL_SIZE=4)
endif ()

if (DEFINED WAL)
  add_definitions(-DWAL=${WAL})
else ()
  add_definitions(-DWAL=1)
endif ()

if (DEFINED LOG_WRITER)
    add_definitions(-DLOG_WRITER=${LOG_WRITER})
else ()
    add_definitions(-DLOG_WRITER=0)
endif ()

if (DEFINED RAFTCC)
  add_definitions(-DRAFTCC=${RAFTCC})
else ()
  add_definitions(-DRAFTCC=1)
endif ()

if (DEFINED ZMQ_PUBSUB)
  add_definitions(-DZMQ_PUBSUB=${ZMQ_PUBSUB})
else ()
  add_definitions(-DZMQ_PUBSUB=0)
endif ()
