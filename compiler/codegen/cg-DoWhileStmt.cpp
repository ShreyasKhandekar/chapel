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

#include "DoWhileStmt.h"

#include "AstVisitor.h"
#include "build.h"
#include "codegen.h"
#include "LayeredValueTable.h"
#include "llvmTracker.h"
#include "llvmVer.h"

#ifdef HAVE_LLVM
#include "llvm/IR/Module.h"
#endif

/************************************ | *************************************
*                                                                           *
* Instance methods                                                          *
*                                                                           *
************************************* | ************************************/

GenRet DoWhileStmt::codegen()
{
  GenInfo* info    = gGenInfo;
  FILE*    outfile = info->cfile;
  GenRet   ret;

  codegenStmt(this);

  reportVectorizable();

  if (outfile)
  {
    info->cStatements.push_back("do ");

    if (this != getFunction()->body)
      info->cStatements.push_back("{\n");

    body.codegen("");

    std::string ftr= "} while (" + codegenValue(condExprGet()).c + ");\n";

    info->cStatements.push_back(ftr);
  }

  else
  {
#ifdef HAVE_LLVM
    llvm::Function*   func             = info->irBuilder->GetInsertBlock()->getParent();

    llvm::BasicBlock* blockStmtBody    = NULL;
    llvm::BasicBlock* blockStmtEnd     = NULL;
    llvm::BasicBlock* blockStmtEndCond = NULL;

    getFunction()->codegenUniqueNum++;

    blockStmtBody = llvm::BasicBlock::Create(info->module->getContext(), FNAME("blk_body"));
    blockStmtEnd  = llvm::BasicBlock::Create(info->module->getContext(), FNAME("blk_end"));
    trackLLVMValue(blockStmtBody);
    trackLLVMValue(blockStmtEnd);

    llvm::BranchInst* toBody = info->irBuilder->CreateBr(blockStmtBody);
    trackLLVMValue(toBody);

    // Now add the body.
#if HAVE_LLVM_VER >= 160
    func->insert(func->end(), blockStmtBody);
#else
    func->getBasicBlockList().push_back(blockStmtBody);
#endif

    info->irBuilder->SetInsertPoint(blockStmtBody);
    info->lvt->addLayer();

    body.codegen("");

    info->lvt->removeLayer();

    // Add the condition block.
    blockStmtEndCond = llvm::BasicBlock::Create(info->module->getContext(), FNAME("blk_end_cond"));
    trackLLVMValue(blockStmtEndCond);

#if HAVE_LLVM_VER >= 160
    func->insert(func->end(), blockStmtEndCond);
#else
    func->getBasicBlockList().push_back(blockStmtEndCond);
#endif

    // Insert an explicit branch from the body block to the loop condition.
    llvm::BranchInst* toCond = info->irBuilder->CreateBr(blockStmtEndCond);
    trackLLVMValue(toCond);

    // set insert point
    info->irBuilder->SetInsertPoint(blockStmtEndCond);

    GenRet       condValueRet = codegenValue(condExprGet());
    llvm::Value* condValue    = condValueRet.val;

    if (condValue->getType() != llvm::Type::getInt1Ty(info->module->getContext()))
    {
      condValue = info->irBuilder->CreateICmpNE(condValue,
                                                llvm::ConstantInt::get(condValue->getType(), 0),
                                                FNAME("condition"));
      trackLLVMValue(condValue);
    }

    llvm::BranchInst* condBr = info->irBuilder->CreateCondBr(condValue, blockStmtBody, blockStmtEnd);
    trackLLVMValue(condBr);

#if HAVE_LLVM_VER >= 160
    func->insert(func->end(), blockStmtEnd);
#else
    func->getBasicBlockList().push_back(blockStmtEnd);
#endif

    info->irBuilder->SetInsertPoint(blockStmtEnd);

    if (blockStmtBody   ) INT_ASSERT(blockStmtBody->getParent()    == func);
    if (blockStmtEndCond) INT_ASSERT(blockStmtEndCond->getParent() == func);
    if (blockStmtEnd    ) INT_ASSERT(blockStmtEnd->getParent()     == func);
#endif
  }

  return ret;
}
