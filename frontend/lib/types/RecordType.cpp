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

#include "chpl/types/RecordType.h"

#include "chpl/framework/query-impl.h"

namespace chpl {
namespace types {


const owned<RecordType>&
RecordType::getRecordType(Context* context, ID id, UniqueString name,
                          const RecordType* instantiatedFrom,
                          SubstitutionsMap subs,
                          CompositeType::Linkage linkage) {
  QUERY_BEGIN(getRecordType, context, id, name, instantiatedFrom, subs,
              linkage);

  auto result = toOwned(new RecordType(id, name,
                                       instantiatedFrom, std::move(subs),
                                       linkage));

  return QUERY_END(result);
}

const RecordType*
RecordType::get(Context* context, ID id, UniqueString name,
                const RecordType* instantiatedFrom,
                SubstitutionsMap subs) {
  auto linkage = parsing::idToDeclLinkage(context, id);
  return getRecordType(context, id, name,
                       instantiatedFrom, std::move(subs), linkage).get();
}


} // end namespace types
} // end namespace chpl
