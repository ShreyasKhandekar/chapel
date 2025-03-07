/*
 * Copyright 2020-2025 Hewlett Packard Enterprise Development LP
 * Copyright 2004-2019 Cray Inc.
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

#include "LoopStmt.h"
#include "ForallStmt.h"

LoopStmt::LoopStmt(BlockStmt* initBody) : BlockStmt(initBody)
{
  mBreakLabel       = 0;
  mContinueLabel    = 0;
  mOrderIndependent = false;
  mExemptFromImplicitIntents = false;
  mVectorizationHazard = false;
  mParallelAccessVectorizationHazard = false;
  mLLVMMetadataList = {};
}

LabelSymbol* LoopStmt::breakLabelGet() const
{
  return mBreakLabel;
}

void LoopStmt::breakLabelSet(LabelSymbol* sym)
{
  mBreakLabel = sym;
}

LabelSymbol* LoopStmt::continueLabelGet() const
{
  return mContinueLabel;
}

void LoopStmt::continueLabelSet(LabelSymbol* sym)
{
  mContinueLabel = sym;
}

bool LoopStmt::isOrderIndependent() const
{
  return mOrderIndependent;
}

void LoopStmt::orderIndependentSet(bool orderIndependent)
{
  mOrderIndependent = orderIndependent;
}

void LoopStmt::exemptFromImplicitIntents() {
  mExemptFromImplicitIntents = true;
}

bool LoopStmt::isExemptFromImplicitIntents() const {
  return mExemptFromImplicitIntents;
}

bool LoopStmt::hasVectorizationHazard() const
{
  return mVectorizationHazard;
}

void LoopStmt::setHasVectorizationHazard(bool v)
{
  mVectorizationHazard = v;
}

bool LoopStmt::hasParallelAccessVectorizationHazard() const
{
  return mParallelAccessVectorizationHazard;
}

void LoopStmt::setHasParallelAccessVectorizationHazard(bool v)
{
  mParallelAccessVectorizationHazard = v;
}


bool LoopStmt::isVectorizable() const
{
  return mOrderIndependent &&
         !mVectorizationHazard;
}

bool LoopStmt::isParallelAccessVectorizable() const
{
  return mOrderIndependent &&
         !mVectorizationHazard &&
         !mParallelAccessVectorizationHazard;
}

bool LoopStmt::hasAdditionalLLVMMetadata() const {
  return !mLLVMMetadataList.empty();
}
bool LoopStmt::hasAdditionalLLVMMetadata(const char* a) const {
  return this->getAdditionalLLVMMetadata(a) != nullptr;
}
const LLVMMetadataList& LoopStmt::getAdditionalLLVMMetadata() const {
  return mLLVMMetadataList;
}
LLVMMetadataPtr LoopStmt::getAdditionalLLVMMetadata(const char* a) const {
  const char* aa = astr(a);
  auto it = std::find_if(mLLVMMetadataList.begin(),
                         mLLVMMetadataList.end(),
                         [aa](auto elm) {return elm->key == aa;});
  return it != mLLVMMetadataList.end() ? *it : nullptr;
}
void LoopStmt::setAdditionalLLVMMetadata(const LLVMMetadataList& al) {
  mLLVMMetadataList = al;
}

// what if the nearest enclosing loop is a forall?
LoopStmt* LoopStmt::findEnclosingLoop(Expr* expr)
{
  LoopStmt* retval = NULL;

  if (LoopStmt* loop = toLoopStmt(expr))
  {
    retval = loop;
  }

  else if (expr->parentExpr)
  {
    retval = findEnclosingLoop(expr->parentExpr);
  }

  else
  {
    retval = NULL;
  }

  return retval;

}

LoopStmt* LoopStmt::findEnclosingLoop(Expr* expr, const char* name)
{
  LoopStmt* retval = LoopStmt::findEnclosingLoop(expr);

  while (retval != NULL && retval->isNamed(name) == false)
  {
    retval = LoopStmt::findEnclosingLoop(retval->parentExpr);
  }

  return retval;
}

Stmt* LoopStmt::findEnclosingLoopOrForall(Expr* expr)
{
  for (Expr* curr = expr; curr != NULL; curr = curr->parentExpr) {
    if (LoopStmt* loop = toLoopStmt(curr)) {
      return loop;
    }
    if (ForallStmt* forall = toForallStmt(curr)) {
      return forall;
    }
  }
  // no enclosing loops
  return NULL;
}

bool LoopStmt::isNamed(const char* name) const
{
  bool retval = false;

  if (userLabel != NULL)
  {
    retval = (strcmp(userLabel, name) == 0) ? true : false;
  }

  return retval;
}
