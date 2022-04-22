#pragma once

#include <iostream>
#include <msgpack.hpp>
#include "procedure.hh"

using std::cout;
using std::endl;

#if 0
enum class Ope : uint8_t {
  READ,
  WRITE,
  READ_MODIFY_WRITE,
};
#endif

class ProcedureX {
public:
  Ope ope_;
  uint64_t key_;
  bool ronly_ = false;
  bool wonly_ = false;
  MSGPACK_DEFINE((uint8_t&)ope_, key_, ronly_, wonly_);

  ProcedureX() : ope_(Ope::READ), key_(0) {}

  ProcedureX(Ope ope, uint64_t key) : ope_(ope), key_(key) {}

  bool operator<(const ProcedureX &right) const {
    if (this->key_ == right.key_ && this->ope_ == Ope::WRITE &&
        right.ope_ == Ope::READ) {
      return true;
    } else if (this->key_ == right.key_ && this->ope_ == Ope::WRITE &&
               right.ope_ == Ope::WRITE) {
      return true;
    }
    /* キーが同値なら先に write ope を実行したい．read -> write よりも write ->
     * read.
     * キーが同値で自分が read でここまで来たら，下記の式によって絶対に false
     * となり，自分 (read) が昇順で後ろ回しになるので ok */

    return this->key_ < right.key_;
  }
};
