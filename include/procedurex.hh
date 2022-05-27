#pragma once

#include <msgpack.hpp>
#include "../third_party/ccbench/include/procedure.hh"

class ProcedureX : public Procedure {
public:
  MSGPACK_DEFINE((uint8_t&)ope_, key_, ronly_, wonly_);
  ProcedureX() : Procedure() {}
  ProcedureX(Ope ope, uint64_t key) : Procedure(ope, key) {}
};
