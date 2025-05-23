/*
 * Copyright 2021-2025 Hewlett Packard Enterprise Development LP
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "chpl/uast/Cobegin.h"

#include "chpl/uast/Builder.h"

namespace chpl {
namespace uast {


std::string Cobegin::dumpChildLabelInner(int i) const {
  if (i == withClauseChildNum_) {
    return "with";
  } else if (i > bodyChildNum_) {
    std::string ret = "task-body ";
    ret += std::to_string(i - bodyChildNum_);
    return ret;
  }

  return "";
}

owned<Cobegin> Cobegin::build(Builder* builder,
                              Location loc,
                              owned<WithClause> withClause,
                              AstList taskBodies) {
  AstList lst;
  int8_t withClauseChildNum = NO_CHILD;

  if (withClause.get() != nullptr) {
    withClauseChildNum = lst.size();
    lst.push_back(std::move(withClause));
  }

  const int bodyChildNum = lst.size();
  const int numTaskBodies = taskBodies.size();

  for (auto& taskBody : taskBodies) {
    lst.push_back(std::move(taskBody));
  }

  Cobegin* ret = new Cobegin(std::move(lst), withClauseChildNum,
                             bodyChildNum,
                             numTaskBodies);
  builder->noteLocation(ret, loc);
  return toOwned(ret);
}


} // namespace uast
} // namespace chpl
