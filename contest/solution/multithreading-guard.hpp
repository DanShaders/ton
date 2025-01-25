#pragma once

#include <string>

#include "contest-validate-query.hpp"

namespace solution {

class ContestValidateQuery;

class MultithreadingGuard {
 public:
  MultithreadingGuard(ContestValidateQuery& cvq, std::string name = "");
  MultithreadingGuard(ContestValidateQuery* cvq, std::string name = "");
  ~MultithreadingGuard();

 private:
  ContestValidateQuery& cvq;
  std::string name;

  void guard();
  void release();
};

}
