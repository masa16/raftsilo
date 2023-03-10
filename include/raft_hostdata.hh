#pragma once
#include <string>
#include <vector>
extern "C" {
  #include "raft.h"
  #include "raft_log.h"
}

class HostData {
public:
  //static const int MSG_TYPE = RAFT_MSG_LEADER_HINT;
  raft_node_id_t id_ = -1;
  std::string hostname_;
  std::uint32_t req_port_ = 0;
  std::uint32_t pub_port_ = 0;
  bool is_leader_ = false;
  //MSGPACK_DEFINE(id_, hostname_, req_port_, pub_port_);

  HostData() {};
  HostData(raft_node_id_t id, std::string hostname,
    std::uint32_t req_port, std::uint32_t pub_port) :
    id_(id), hostname_(hostname), req_port_(req_port), pub_port_(pub_port) {}
  HostData(raft_node_id_t id, std::string hostname,
    std::uint32_t req_port, std::uint32_t pub_port, std::string state) :
    id_(id), hostname_(hostname), req_port_(req_port), pub_port_(pub_port) {
    std::string s;
    s.resize(state.size());
    std::transform(state.cbegin(), state.cend(), s.begin(),
      [](unsigned char c){ return std::toupper(c); });
    is_leader_ = (s=="L" || s=="LEADER");
  }
  HostData(raft_node_id_t id, std::string hostname) :
    id_(id), hostname_(hostname) {
    req_port_ = 51110 + id_*2;
    pub_port_ = 51111 + id_*2;
  }
};
