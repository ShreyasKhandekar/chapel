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

#include "chpl/uast/Include.h"

#include "chpl/uast/Builder.h"

namespace chpl {
namespace uast {


void Include::dumpFieldsInner(const DumpSettings& s) const {
  const char* vis = Decl::visibilityToString(visibility_);
  if (vis[0]) {
    s.out << " " << vis;
  }
  if (isPrototype_) {
    s.out << " prototype";
  }
  s.out << " " << name_.str();
}

owned<Include> Include::build(Builder* builder, Location loc,
                              Decl::Visibility visibility,
                              bool isPrototype,
                              UniqueString name, Location nameLoc) {
  Include* ret = new Include(visibility, isPrototype, name);
  builder->noteLocation(ret, loc);
  builder->noteIncludeNameLocation(ret, nameLoc);
  return toOwned(ret);
}


} // namespace uast
} // namespace chpl
