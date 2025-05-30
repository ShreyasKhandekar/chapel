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

#include "chpl/framework/ID.h"

#include "chpl/framework/update-functions.h"
#include "chpl/resolution/scope-types.h"
#include "chpl/resolution/scope-queries.h"
#include "chpl/uast/AstTag.h"

#include <cstring>

namespace chpl {

UniqueString ID::symbolPathWithoutRepeats(Context* context) const {
  std::string ret = "";
  bool needsDot = false;

  auto expanded = expandSymbolPath(context, symbolPath_);
  for (auto pathPair : expanded) {
    if (needsDot) ret += ".";
    needsDot = true;

    ret += pathPair.first.c_str();
  }

  return UniqueString::get(context, ret.c_str());
}

// find the last '.' but don't count \.
// returns -1 if none was found
static ssize_t findLastDot(const char* path) {
  ssize_t lastDot = -1;
  for (ssize_t i = 1; path[i]; i++) {
    if (path[i] == '.' && path[i-1] != '\\')
      lastDot = i;
  }

  return lastDot;
}

UniqueString ID::parentSymbolPath(Context* context, UniqueString symbolPath) {

  // If the symbol path is empty, return an empty string
  if (symbolPath.isEmpty()) {
    return UniqueString();
  }

  // find the last '.' component from the ID but don't count \.
  const char* path = symbolPath.c_str();
  ssize_t lastDot = findLastDot(path);

  if (lastDot == -1) {
    // no path component to remove, so return an empty string
    return UniqueString();
  }

  // Otherwise, construct a UniqueString for the parent symbol path
  return UniqueString::get(context, path, lastDot);
}

static ssize_t findPoundOrEnd(const char* path) {
  ssize_t end = 0;
  ssize_t i = 1;
  while (true) {
    if (path[i] == '\0' ||
        (path[i] == '#' && path[i-1] != '\\')) {
      end = i;
      break;
    }
    i++;
  }
  return end;
}

static const char* findPathAfterLastDot(const char* path) {
  ssize_t lastDot = findLastDot(path);

  if (lastDot != -1) {
    // skip past the final '.'
    path = path + lastDot + 1;
  }

  return path;
}

UniqueString ID::innermostSymbolName(Context* context, UniqueString symbolPath)
{
  // If the symbol path is empty, return an empty string
  if (symbolPath.isEmpty()) {
    return UniqueString();
  }

  // find the last '.' component from the ID but don't count \.
  const char* path = findPathAfterLastDot(symbolPath.c_str());

  // now find truncate it at a '#' but don't count \#
  ssize_t end = findPoundOrEnd(path);

  // compute the portion of the string before the #
  auto s = std::string(path, end);

  // now unquote
  s = unescapeStringId(s);

  // now get the UniqueString
  return UniqueString::get(context, s);
}

UniqueString ID::overloadPart(Context* context,
                              UniqueString symbolPath)
{
  if (symbolPath.isEmpty()) {
    return UniqueString();
  }

  // find the last '.' component from the ID but don't count \.
  const char* path = findPathAfterLastDot(symbolPath.c_str());

  // now find truncate it at a '#' but don't count \#
  ssize_t end = findPoundOrEnd(path);

  // advance past the pound if we aren't at the end of the string.
  // This way, we always start at the beginning of the overload part,
  // if there is one, and otherwise we start at the end of the string.
  if (path[end] == '#') {
    end++;
  }

  // compute the portion of the string after the '#'
  auto s = std::string(path + end);

  // now unquote
  s = unescapeStringId(s);

  // now get the UniqueString
  return UniqueString::get(context, s);
}

ID ID::fabricateId(Context* context,
                   ID parentSymbolId,
                   UniqueString name,
                   FabricatedIdKind kind) {

  auto newSymPath = UniqueString::getConcat(context,
                                            parentSymbolId.symbolPath().c_str(),
                                            ".",
                                            name.c_str());
  auto newId = ID(newSymPath, (int) kind, 0);
  CHPL_ASSERT(newId.isFabricatedId());
  CHPL_ASSERT(newId.fabricatedIdKind() == kind);
  return newId;
}

ID ID::generatedId(UniqueString symbolPath,
                 int postOrderId,
                 int numChildIds) {
  int pid;
  if (postOrderId == -1) {
    pid = ID_GEN_START;
  } else {
    pid = ID_GEN_START - 1 - postOrderId;
  }

  return ID(symbolPath, pid, numChildIds);
}


ID ID::parentSymbolId(Context* context) const {
  UniqueString pathToUse;
  if (postOrderId() >= 0) {
    pathToUse = symbolPath_;
  } else {
    pathToUse = ID::parentSymbolPath(context, symbolPath_);
    if (pathToUse.isEmpty()) {
      // no parent symbol path so return an empty ID
      return ID();
    }
  }

  // Assumption: Generated uAST symbols that define a scope do not themselves
  // contain symbols that define a scope. This means that if we see a generated
  // scope-defining symbol, its parent must not be generated uAST.
  if (this->isFabricatedId() &&
      this->fabricatedIdKind() == FabricatedIdKind::Generated &&
      !isSymbolDefiningScope()) {
    return ID(pathToUse, ID_GEN_START, 0);
  } else {
    // Otherwise, construct an ID for the parent symbol
    return ID(pathToUse, -1, 0);
  }
}

UniqueString ID::symbolName(Context* context) const {
  return ID::innermostSymbolName(context, symbolPath_);
}

UniqueString ID::overloadPart(Context* context) const {
  return ID::overloadPart(context, symbolPath_);
}

bool ID::contains(const ID& other) const {
  UniqueString thisPath = this->symbolPath();
  UniqueString otherPath = other.symbolPath();

  if (thisPath == otherPath) {
    // the nodes have the same parent symbol, so consider the AST ids
    int thisId  = this->postOrderId();
    int thisNContained = this->numContainedChildren();
    int otherId = other.postOrderId();
    int thisFirstContained = thisId - thisNContained;

    return thisId == -1 ||
           (thisFirstContained <= otherId && otherId <= thisId);
  } else {
    if (!otherPath.startsWith(thisPath)) {
      // thisPath is not a prefix of otherPath
      return false;
    } else if (this->postOrderId() != -1) {
      // thisPath is a prefix of otherPath, but...
      // E.g. MyModule@5 can't contain MyModule.function
      return false;
    } else if (otherPath.c_str()[thisPath.length()] != '.') {
      // thisPath is a prefix of otherPath, but...
      // E.g. MyMod doesn't contain MyModule
      return false;
    } else {
      return true;
    }
  }
}

int ID::compare(const ID& other) const {
  // first, compare with the path portion
  UniqueString lhsPath = this->symbolPath();
  UniqueString rhsPath = other.symbolPath();
  int pathCmp = lhsPath.compare(rhsPath);
  if (pathCmp != 0)
    return pathCmp;

  // if that wasn't different, compare the id
  return this->postOrderId() - other.postOrderId();

  // numChildIds_ is intentionally not compared
}

std::vector<std::pair<UniqueString,int>>
ID::expandSymbolPath(Context* context, UniqueString symbolPath) {
  std::vector<std::pair<UniqueString,int>> ret;

  const char* s = symbolPath.c_str();
  while (s && s[0] != '\0') {
    const char* nextPeriod = nullptr;
    // find the next period, but don't count \.
    {
      const char* p = s;
      char cur = 0;
      char last = 0;
      while (true) {
        cur = *p;
        if (cur == '\0') {
          break; // end of string
        }
        if (cur == '.' && last != '\\') {
          nextPeriod = p;
          break; // found the period
        }
        last = cur;
        p++;
      }
    }

    // compute location of the null byte or just after the next '.'
    const char* next = nullptr;
    if (nextPeriod == nullptr) {
      // location of the null byte
      next = s + strlen(s);
    } else {
      // just after the '.'
      next = nextPeriod + 1;
    }

    // if there is a '#' in s..next, find it,
    // but don't count \#
    const char* nextPound = nullptr;
    {
      char cur = 0;
      char last = 0;
      for (const char* p = s; p < next; p++) {
        cur = *p;
        if (cur == '#' && last != '\\') {
          nextPound = p;
          break;
        }
        last = cur;
      }
    }

    // compute the repeat count
    int repeat = 0;
    if (nextPound != nullptr) {
      // convert the numeric repeat value to an integer
      // note that atoi should stop at the first non-digit
      repeat = atoi(nextPound + 1);
    }

    // compute location of the '#', '.', or the null byte,
    // whichever comes first
    const char* partEnd = nullptr;
    if (nextPound != nullptr) {
      partEnd = nextPound;
    } else if (nextPeriod != nullptr) {
      partEnd = nextPeriod;
    } else {
      partEnd = next; // next is location of null byte in this case
    }

    // compute the UniqueString containing just the symbol part
    auto part = UniqueString::get(context, s, partEnd-s);

    ret.emplace_back(part, repeat);

    s = next;
  }

  return ret;
}

bool ID::update(chpl::ID& keep, chpl::ID& addin) {
  return defaultUpdate(keep, addin);
}

void ID::stringify(std::ostream& ss,
                   chpl::StringifyKind stringKind) const {
  ss << this->symbolPath().c_str();

  if (!(symbolPath().isEmpty()) && this->postOrderId() >= 0) {
    ss << "@";
    ss << std::to_string(this->postOrderId());
  }
}

IMPLEMENT_DUMP(ID);

std::string ID::str() const {
  std::ostringstream ss;
  stringify(ss, chpl::StringifyKind::DEBUG_SUMMARY);
  return ss.str();
}

ID ID::fromString(Context* context, const char* idStr) {
  if (idStr == nullptr || idStr[0] == '\0') {
    return ID();
  }

  int atPos = 0;
  for (atPos = 0; idStr[atPos]; atPos++) {
    if (idStr[atPos] == '@')
      break;
  }

  // compute the path part (not counting the '@' part)
  auto symPath = UniqueString::get(context, idStr, atPos);

  int postorder = -1;

  if (idStr[atPos] == '@') {
    postorder = std::stoi(&idStr[atPos+1]);
  }

  return ID(symPath, postorder, /* num child IDs */ -1);
}


} // end namespace chpl
