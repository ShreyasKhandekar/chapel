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

#include "expr.h"

#include "alist.h"
#include "astutil.h"
#include "AstVisitor.h"
#include "clangUtil.h"
#include "codegen.h"
#include "driver.h"
#include "ForLoop.h"
#include "genret.h"
#include "insertLineNumbers.h"
#include "LayeredValueTable.h"
#include "llvmUtil.h"
#include "misc.h"
#include "passes.h"
#include "stmt.h"
#include "stringutil.h"
#include "type.h"
#include "virtualDispatch.h"
#include "WhileStmt.h"
#include "wellknown.h"

#ifdef HAVE_LLVM
#include "llvm/IR/Module.h"
#include "llvmTracker.h"
#include "llvmUtil.h"
#include "llvm/IR/IntrinsicsNVPTX.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#endif

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <stack>

class FnSymbol;

// some prototypes
static void codegenAssign(GenRet to_ptr, GenRet from);
static GenRet codegenCast(Type* t, GenRet value, bool Cparens = true);
static GenRet codegenCastToVoidStar(GenRet value);
static bool codegenIsSpecialPrimitive(BaseAST* target, Expr* e, GenRet& ret);
static GenRet maybeConvertToLocalPointer(Expr* expr, GenRet& act);

// These functions operate on wide pointers. There are several different
// kinds of wide pointers:
//  1 wide reference to something
//    (wide.chplType->symbol->hasFlag(FLAG_WIDE_REF))
//  2 wide class pointer
//    (wide.chplType->symbol->hasFlag(FLAG_WIDE_CLASS))
//  3 wide result of codegenFieldPtr or codegenElementPtr etc
//    (wide.isLVPtr == GEN_WIDE_PTR)
// These functions need to handle all of these cases, but they
//  may transform the last case into the 1st. They can't transform
//  case 2 into case 3 because we wouldn't have a Chapel type for
//  the body of a class.
static GenRet codegenRaddr(GenRet wide);
static GenRet codegenRlocale(GenRet wide);
static GenRet codegenRnode(GenRet wide);

static GenRet codegenAddrOf(GenRet r);

/* Note well the difference between codegenCall and codegenCallExpr.
 * codegenCallExpr always returns the call as an expression in the
 * returned GenRet. But codegenCall instead adds the call to the
 * generated statements. If one uses codegenCallExpr instead of codegenCall,
 * the C backend will never actually emit the call, since it won't
 * be added to the list of statements.
 */

static GenRet codegenCallExprInner(GenRet function,
                                   std::vector<GenRet> & args,
                                   FnSymbol* fn,
                                   ClangFunctionDeclPtr FD,
                                   LlvmFunctionTypePtr fnTy,
                                   bool defaultToValues);
static GenRet codegenCallExprWithArgs(GenRet function,
                                      std::vector<GenRet> & args,
                                      const char* fnName,
                                      FnSymbol* fnSym,
                                      ClangFunctionDeclPtr FD,
                                      bool defaultToValues);
static void codegenCallWithArgs(const char* fnName,
                                std::vector<GenRet> & args,
                                FnSymbol* fnSym = nullptr,
                                ClangFunctionDeclPtr FD = nullptr,
                                bool defaultToValues = true);

// some codegenCallExpr are declared in codegen.h
static GenRet codegenCallExpr(const char* fnName, GenRet a1, GenRet a2, GenRet a3);
static void codegenCall(const char* fnName, GenRet a1);
static void codegenCall(const char* fnName, GenRet a1, GenRet a2);
static void codegenCall(const char* fnName, GenRet a1, GenRet a2, GenRet a3);
static void codegenCall(const char* fnName, GenRet a1, GenRet a2, GenRet a3, GenRet a4);
//static void codegenCall(const char* fnName, GenRet a1, GenRet a2, GenRet a3, GenRet a4, GenRet a5);
static void codegenCall(const char* fnName, GenRet a1, GenRet a2, GenRet a3, GenRet a4, GenRet a5, GenRet a6);

static GenRet codegenZero();
static GenRet codegenZero32();
static GenRet codegen_prim_get_real(GenRet, Type*, bool real);

static int codegen_tmp = 1;

/************************************ | *************************************
*                                                                           *
*                                                                           *
************************************* | ************************************/



/************************************ | *************************************
*                                                                           *
*                                                                           *
************************************* | ************************************/

#ifdef HAVE_LLVM
static void addNoAliasMetadata(GenRet &ret, Symbol* sym) {
  GenInfo* info = gGenInfo;
  if (info->cfile == NULL) {
    // add no-alias information if it's in our map
    if (info->noAliasScopeLists.count(sym) > 0)
      ret.aliasScope = info->noAliasScopeLists[sym];
    if (info->noAliasLists.count(sym) > 0)
      ret.noalias = info->noAliasLists[sym];
  }
}
#endif

#ifdef HAVE_LLVM
static bool shouldGenerateAsIfCallingDirectly(SymExpr* se, FnSymbol* fn) {
  INT_ASSERT(se->symbol() == fn);

  // The function in question is never used to create a 'function value',
  // (so it cannot possibly be called indirectly), so just return 'true'.
  if (!fn->hasFlag(FLAG_FIRST_CLASS_FUNCTION_INVOCATION)) return true;

  if (auto call = toCallExpr(se->parentExpr)) {
    // Otherwise, if it's a call base expression or being casted to a pointer,
    // then we should generate it as if calling it directly to manifest the
    // address on the current locale.
    bool isCallBaseExpr = (se == call->baseExpr);
    bool isCastedToPtr = call->isPrimitive(PRIM_CAST_TO_TYPE) ||
                         call->isPrimitive(PRIM_CAST);
    bool ret = isCallBaseExpr || isCastedToPtr;
    return ret;
  }

  // Otherwise, treat the mention as indirect.
  return false;
}
#endif

GenRet SymExpr::codegen() {
  GenInfo* info = gGenInfo;
  FILE* outfile = info->cfile;
  GenRet ret;

  if (id == breakOnCodegenID)
    debuggerBreakHere();

  if( outfile ) {
    if (getStmtExpr() && getStmtExpr() == this)
      codegenStmt(this);
    ret = var->codegen();
  } else {
#ifdef HAVE_LLVM
    if(isVarSymbol(var)) {
      ret = toVarSymbol(var)->codegen();
      addNoAliasMetadata(ret, var);
      if (var->hasFlag(FLAG_POINTS_OUTSIDE_ORDER_INDEPENDENT_LOOP))
        ret.mustPointOutsideOrderIndependentLoop = true;
    } else if(isArgSymbol(var)) {
      ret = toArgSymbol(var)->codegen();
      addNoAliasMetadata(ret, var);
    } else if(isTypeSymbol(var)) {
      ret.type = toTypeSymbol(var)->codegen().type;
    } else if(auto fn = toFnSymbol(var)) {
      if (shouldGenerateAsIfCallingDirectly(this, fn)) {
        ret = fn->codegenAsCallBaseExpr();
      } else {
        ret = fn->codegenAsValue();
      }
    } else {
      ret = info->lvt->getValue(var->cname);
      if( ! ret.val ) {
        INT_FATAL(this, "!!!!!!! UNHANDLED SYM EXPR !!!!!!!");
      }
    }
#endif
  }
  return ret;
}

/************************************ | *************************************
*                                                                           *
*                                                                           *
************************************* | ************************************/

GenRet UnresolvedSymExpr::codegen() {
  GenInfo* info = gGenInfo;
  FILE* outfile = info->cfile;
  GenRet ret;
  INT_FATAL(this, "UnresolvedSymExpr::codegen called");
  if( outfile ) fprintf(outfile, "%s /* unresolved symbol */", unresolved);
  return ret;
}

/************************************ | *************************************
*                                                                           *
*                                                                           *
************************************* | ************************************/

GenRet DefExpr::codegen() {
  GenInfo* info = gGenInfo;
  GenRet ret;
  if( info->cfile ) {
    if (toLabelSymbol(sym)) {
      std::string str = sym->cname;
      str += ":;\n"; // ; avoids 'label at end of compound statement' error
      info->cStatements.push_back(str);
    }
  } else {
#ifdef HAVE_LLVM
    if (toLabelSymbol(sym)) {
      llvm::Function *func = info->irBuilder->GetInsertBlock()->getParent();

      llvm::BasicBlock *blockLabel;

      if(!(blockLabel = info->lvt->getBlock(sym->cname))) {
        blockLabel = llvm::BasicBlock::Create(
            info->module->getContext(), sym->cname);
        trackLLVMValue(blockLabel);
        info->lvt->addBlock(sym->cname, blockLabel);
      }

      llvm::BranchInst* brInst = info->irBuilder->CreateBr(blockLabel);
      trackLLVMValue(brInst);

#if HAVE_LLVM_VER >= 160
      func->insert(func->end(), blockLabel);
#else
      func->getBasicBlockList().push_back(blockLabel);
#endif
      info->irBuilder->SetInsertPoint(blockLabel);
    }
#endif
  }
  return ret;
}

/************************************ | *************************************
*                                                                           *
*                                                                           *
************************************* | ************************************/

#ifdef HAVE_LLVM
llvm::AllocaInst* createVarLLVM(llvm::Type* type, Type* astType,
                                Symbol* astSymbol, const char* name)
{
  GenInfo* info = gGenInfo;
  llvm::IRBuilder<>* irBuilder = info->irBuilder;
  llvm::AllocaInst* val = NULL;

  val = createAllocaInFunctionEntry(irBuilder, type, name);
  setValueAlignment(val, astType, astSymbol);

  return val;
}

llvm::AllocaInst* createVarLLVM(llvm::Type* type, Type* astType,
                                Symbol* astSymbol)
{
  char name[32];
  snprintf(name, sizeof(name), "chpl_macro_tmp_%d", codegen_tmp++);
  return createVarLLVM(type, astType, astSymbol, name);
}

// 'alignment' is expected to follow 'AlignmentStatus'
llvm::AllocaInst* createVarLLVM(llvm::Type* type, const char* name,
                                int alignment)
{
  llvm::AllocaInst* result = createVarLLVM(type, (Type*)nullptr,
                                           (Symbol*)nullptr, name);
  setValueAlignment(result, alignment);
  return result;
}

llvm::Value *convertValueToType(llvm::Value *value, llvm::Type *newType,
                                bool isSigned, bool force) {
  GenInfo* info = gGenInfo;
  llvm::IRBuilder<>* irBuilder = info->irBuilder;
  const llvm::DataLayout& layout = info->module->getDataLayout();
  llvm::LLVMContext &ctx = gContext->llvmContext();
  llvm::Value* val = NULL;
  llvm::AllocaInst* alloca = NULL;

  val = convertValueToType(irBuilder, layout, ctx,
                            value, newType,
                            &alloca, isSigned, force);

  if (alloca != NULL)
    info->currentStackVariables.push_back(
      std::pair<llvm::AllocaInst*, llvm::Type*>(alloca, newType));

  return val;
}

// 'alignment' is expected to follow 'AlignmentStatus'
void setValueAlignment(llvm::Value* value, int alignment) {
  if (! isDeferredAlignment(alignment)) {
    llvm::Align align = llvm::Align(alignment);
    // todo: avoid a branch when compiler knows the arg is a GlobalVariable
    if (auto val = llvm::dyn_cast<llvm::AllocaInst>(value))
      val->setAlignment(align);
    else if (auto val = llvm::dyn_cast<llvm::GlobalVariable>(value))
      val->setAlignment(align);
    else
      INT_FATAL("unexpected llvm::Value");
  }
}

// 'astSymbol' and 'astType' are used to determine the alignment.
// 'astSymbol' can be null when called from createTempVar()
// 'astType' can be also null when called from createTempVar(ctype, alignment)
void setValueAlignment(llvm::Value* value, Type* astType, Symbol* astSymbol) {
  // Future work: we have propagated 'astSymbol' all the way to here so that
  // we can check its desired alignment, in addition to astType's, if any.
  if (astType == nullptr) return;
  setValueAlignment(value, astType->getLLVMAlignment());
}

static
PromotedPair convertValuesToLarger(llvm::Value *value1, llvm::Value *value2, bool isSigned1 = false, bool isSigned2 = false)
{
  GenInfo* info = gGenInfo;
  return convertValuesToLarger(info->irBuilder,
                               value1, value2, isSigned1, isSigned2);
}

// Sign or zero extend a value to an integer that is the size of a
// pointer in the address space AS.
static llvm::Value* extendToPointerSize(GenRet index, unsigned AS) {
  GenInfo* info = gGenInfo;

  const llvm::DataLayout& DL = info->module->getDataLayout();
  llvm::Type* sizeTy = DL.getIntPtrType(info->module->getContext(), AS);
  unsigned sizeBits = DL.getTypeSizeInBits(sizeTy);
  unsigned idxBits = DL.getTypeSizeInBits(index.val->getType());

  if (idxBits < sizeBits) {
    return convertValueToType(index.val, sizeTy, !index.isUnsigned);
  }
  return index.val;
}


static llvm::Value* createInBoundsGEP(llvm::Type* gepType,
                                      llvm::Value* ptr,
                                      llvm::ArrayRef<llvm::Value*> idxList) {
  GenInfo* info = gGenInfo;

  INT_ASSERT(gepType);
  INT_ASSERT(ptr);
  INT_ASSERT(idxList.size() > 0);

  if (developer || fVerify) {
    const llvm::DataLayout& DL = info->module->getDataLayout();
    unsigned ptrBits = DL.getPointerSizeInBits(0);
    // Check that each idxList element is at least ptrBits big.
    // Otherwise, it always does signed extending, but sometimes
    // we want unsigned.
    for (auto v : idxList) {
      unsigned idxSize = DL.getTypeSizeInBits(v->getType());
      INT_ASSERT(idxSize >= ptrBits);
      // consider calling extendToPointerSize at the call site
    }
  }
  llvm::Value* gep = info->irBuilder->CreateInBoundsGEP(gepType, ptr, idxList);
  trackLLVMValue(gep);
  return gep;
}

#endif

enum WideThingField {
  WIDE_GEP_LOC=0,
  WIDE_GEP_ADDR=1,
};

static const char* wide_fields[] = {"locale", "addr", "size", NULL};

static GenRet genCommID(GenInfo* info) {
  return baseASTCodegen(new_CommIDSymbol(commIDMap[info->filename]++));
}

// Generates code to load the wide version of an address and returns an
// expression that evaluates to this address.
//
// The wide address is generated by a call to the runtime support
// function chpl_build_wide_ptr_loc.
//
// The type can be passed here in case the raddr does not have a Chapel type
// or in case it would be difficult to compute it. It used to be the case that
// it was sometimes impossible to reference types for some arguments but
// getOrMakeRefTypeDuringCodegen/getOrMakeWideTypeDuringCodegen may cover
// all the cases.
static
GenRet codegenWideAddr(GenRet locale, GenRet raddr, Type* wideType = NULL)
{
  GenRet ret;
  GenInfo* info = gGenInfo;
  Type* wideRefType = NULL; // either a wide class or a wide ref

  if( locale.chplType ) {
    INT_ASSERT(locale.chplType->getValType() == dtLocaleID);
  }

  if( raddr.chplType && !wideType ) {
    INT_ASSERT(raddr.isLVPtr != GEN_WIDE_PTR);
    Type* refType = NULL;
    if( raddr.isLVPtr == GEN_VAL ) {
      // Then we should have a ref or a class.
      INT_ASSERT(raddr.chplType == dtNil ||
                 isClass(raddr.chplType) ||
                 raddr.chplType->symbol->hasFlag(FLAG_REF));
      refType = raddr.chplType;
    } else {
      // GEN_REF
      refType = getOrMakeRefTypeDuringCodegen(raddr.chplType);
    }
    wideRefType = getOrMakeWideTypeDuringCodegen(refType);
    INT_ASSERT(wideRefType);
  } else {
    wideRefType = wideType;
  }

  INT_ASSERT(wideRefType);

  locale = codegenValue(locale);
  if (locale.chplType && locale.chplType->isRef()) {
    locale = codegenValue(codegenDeref(locale));
  }
  if( !fLLVMWideOpt ) {
    // Create a stack-local stored wide pointer
    // of the appropriate type.
    ret = createTempVar(wideRefType);
    if( info->cfile ) {
      std::string localeAssign;
      std::string addrAssign;
      ret = codegenValue(ret); // remove the & part.
      localeAssign = ret.c + ".locale = " + locale.c + ";\n";
      info->cStatements.push_back(localeAssign);
      addrAssign = ret.c + ".addr = " + raddr.c + ";\n";
      info->cStatements.push_back(addrAssign);
    } else {
#ifdef HAVE_LLVM
      GenRet genWideRefType = wideRefType;
      llvm::Type* gepTy = genWideRefType.type;
      INT_ASSERT(gepTy);

      llvm::Value* adr = info->irBuilder->CreateStructGEP(
          gepTy, ret.val, WIDE_GEP_ADDR);
      llvm::Value* loc = info->irBuilder->CreateStructGEP(
          gepTy, ret.val, WIDE_GEP_LOC);
      trackLLVMValue(adr);
      trackLLVMValue(loc);

      llvm::Value* addrVal = raddr.val;

#ifdef HAVE_LLVM_TYPED_POINTERS
      if (!isOpaquePointer(adr->getType())) {
        // cast address if needed. This is necessary for building a wide
        // NULL pointer since NULL is actually an i8*.
        llvm::Type* addrType = adr->getType()->getPointerElementType();
        if (raddr.val->getType() != addrType) {
          addrVal = convertValueToType(addrVal, addrType);
        }
      }
      INT_ASSERT(addrVal);
#endif

      llvm::StoreInst* st1 = info->irBuilder->CreateStore(addrVal, adr);
      llvm::StoreInst* st2 = info->irBuilder->CreateStore(locale.val, loc);
      trackLLVMValue(st1);
      trackLLVMValue(st2);
#endif
    }
    // Load whatever we stored...
    ret = codegenValue(ret);
  } else {
#ifdef HAVE_LLVM
    GenRet wideTy = wideRefType; // get the LLVM type for the wide ref.
    llvm::PointerType *addrType = llvm::cast<llvm::PointerType>(wideTy.type);

    // call GLOBAL_FN_GLOBAL_MAKE dummy function
    llvm::Function* fn = getMakeFn(info->module, &info->globalToWideInfo,
                                   addrType);
    INT_ASSERT(fn);

    llvm::Type* locAddrType = nullptr;

    if (isOpaquePointer(addrType)) {
      locAddrType = llvm::PointerType::getUnqual(gContext->llvmContext());
    } else {
#ifdef HAVE_LLVM_TYPED_POINTERS
      locAddrType =
        llvm::PointerType::getUnqual(addrType->getPointerElementType());
#endif
    }
    INT_ASSERT(locAddrType);

    // Null pointers require us to possibly cast to the pointer type
    // we are supposed to have since null has type void*.
    llvm::Value* locAddr = raddr.val;
    locAddr = info->irBuilder->CreatePointerCast(locAddr, locAddrType);
    trackLLVMValue(locAddr);
    ret.val = info->irBuilder->CreateCall(fn, {locale.val, locAddr});
    trackLLVMValue(ret.val);
#endif
  }

  ret.chplType = wideRefType->getValType();
  // Class pointers are "values" as far as the code generator
  // is concerned, unlike wide references.
  if (wideRefType->symbol->hasFlag(FLAG_WIDE_CLASS))
    ret.isLVPtr = GEN_VAL;
  else
    ret.isLVPtr = GEN_WIDE_PTR;
  return ret;
}

// Generates a new version of a wide address which has a different
// .addr part, leaving the locale part alone.
static
GenRet codegenWideAddrWithAddr(GenRet base, GenRet newAddr, Type* wideType = NULL)
{
  // NOTE - if computing the entire localeID becomes one day
  // expensive, and it can be inferred from the pointer part,
  // update this to just use the node part.
  return codegenWideAddr(codegenRlocale(base), newAddr, wideType);
}

#ifdef HAVE_LLVM

// Set USE_TBAA to 0 to disable the emission of Type Based Alias Analysis
// metadata when generating LLVM loads or stores.
// Set USE_TBAA to 1 to emit TBAA metadata with loads and stores.
#define USE_TBAA 1

static
llvm::Type* computePointerElementType(llvm::Value* ptr, Type* chplType) {
  llvm::Type* eltTypeFromInsn = nullptr;
  llvm::Type* eltTypeFromChapelType = nullptr;

  if (chplType) {
    // if the pointer has isLVPtr == GEN_PTR, then we expect the
    // .chplType to be the pointed-to type, which is what we are
    // looking for here.
    GenRet genType = chplType;
    eltTypeFromChapelType = genType.type;
    INT_ASSERT(eltTypeFromChapelType);
    return eltTypeFromChapelType;
  }

  // otherwise, try to compute it from the pointer element type.
  // Note that while this works in simple cases, it is inaccurate for
  // more complex cases like e.g. a GEP of the 1st element in a global
  // of struct type -- that can return the global since the opaque pointer
  // value is the same as an opaque pointer pointing to the 1st field.
  eltTypeFromInsn = tryComputingPointerElementType(ptr);
  if (!eltTypeFromInsn) {
    INT_FATAL("could not compute pointer type");
  }
  return eltTypeFromInsn;
}

// If valType is nullptr, will try to compute it by looking
// for an AllocaInst or a GlobalValue in addr.
static
void codegenInvariantStart(llvm::Type *valType, llvm::Value *addr)
{
  GenInfo *info = gGenInfo;
  const llvm::DataLayout& dataLayout = info->module->getDataLayout();

  if (valType == nullptr) {
    valType = tryComputingPointerElementType(addr);
    INT_ASSERT(valType);
  }

  uint64_t sizeInBytes;
  if (valType->isSized())
    sizeInBytes = dataLayout.getTypeSizeInBits(valType)/8;
  else
    return;

  llvm::ConstantInt *size = llvm::ConstantInt::getSigned(
      llvm::Type::getInt64Ty(gContext->llvmContext()), sizeInBytes);

  llvm::CallInst* istart = info->irBuilder->CreateInvariantStart(addr, size);
  trackLLVMValue(istart);
}

// Create an LLVM store instruction possibly adding
// appropriate metadata based upon the Chapel type of val.
//
static
llvm::StoreInst* codegenStoreLLVM(llvm::Value* val,
                                  llvm::Value* ptr,
                                  Type* valType = NULL,
                                  Type* surroundingStruct = NULL,
                                  uint64_t fieldOffset = 0,
                                  llvm::MDNode* fieldTbaaTypeDescriptor = NULL,
                                  llvm::MDNode* aliasScope = NULL,
                                  llvm::MDNode* noalias = NULL,
                                  bool isStoreOfLocalVar = true)
{
  GenInfo *info = gGenInfo;
  llvm::StoreInst* ret = info->irBuilder->CreateStore(val, ptr);
  trackLLVMValue(ret);
  llvm::MDNode* tbaa = NULL;
  if (USE_TBAA && valType &&
      (isClass(valType) || !valType->symbol->llvmTbaaStructCopyNode)) {
    if (surroundingStruct) {
      INT_ASSERT(fieldTbaaTypeDescriptor != info->tbaaRootNode);
      tbaa = info->mdBuilder->createTBAAStructTagNode(
               surroundingStruct->symbol->llvmTbaaAggTypeDescriptor,
               fieldTbaaTypeDescriptor, fieldOffset);
    } else {
      tbaa = valType->symbol->llvmTbaaAccessTag;
    }
  }
  if (tbaa)
    ret->setMetadata(llvm::LLVMContext::MD_tbaa, tbaa);
  if (aliasScope)
    ret->setMetadata(llvm::LLVMContext::MD_alias_scope, aliasScope);
  if (noalias)
    ret->setMetadata(llvm::LLVMContext::MD_noalias, noalias);


  if (isStoreOfLocalVar == false && !info->loopStack.empty()) {
    const auto &loopData = info->loopStack.back();
    if (loopData.markMemoryOps)
      ret->setMetadata("llvm.access.group", loopData.accessGroup);
  }

  return ret;
}



static
llvm::StoreInst* codegenStoreLLVM(GenRet val,
                                  GenRet ptr,
                                  Type* valType = NULL)
{
  if( val.chplType && !valType ) valType = val.chplType;
  if( ptr.chplType && !valType ) {
    if( ptr.isLVPtr ) valType = ptr.chplType;
    else valType = ptr.chplType->getValType();
  }

  llvm::Type* storeType = computePointerElementType(ptr.val, ptr.chplType);
  if (storeType && storeType != val.val->getType()) {
    // check that the stored type matches the valType

    // implicit cast in C, needs to be made explicit in LLVM
    // e.g. T3 = alloca i8;
    //      T3 = (T == T2);   // not actual LLVM syntax
    // in LLVM, boolean type is i1

    llvm::Value* v = convertValueToType(val.val, storeType, !val.isUnsigned);
    INT_ASSERT(v);
    val.val = v;
  }

  return codegenStoreLLVM(val.val, ptr.val, valType, ptr.surroundingStruct,
                          ptr.fieldOffset, ptr.fieldTbaaTypeDescriptor,
                          ptr.aliasScope,
                          ptr.noalias,
                          !ptr.mustPointOutsideOrderIndependentLoop);
}
// Create an LLVM load instruction possibly adding
// appropriate metadata based upon the Chapel type of ptr.
static
llvm::LoadInst* codegenLoadLLVM(llvm::Value* ptr,
                                Type* valType,
                                Type* surroundingStruct = NULL,
                                uint64_t fieldOffset = 0,
                                llvm::MDNode* fieldTbaaTypeDescriptor = NULL,
                                llvm::MDNode* aliasScope = NULL,
                                llvm::MDNode* noalias = NULL,
                                bool isConst = false,
                                bool isLoadOfLocalVar = true)
{
  GenInfo* info = gGenInfo;

  llvm::Type* loadType = computePointerElementType(ptr, valType);
  llvm::LoadInst* ret = info->irBuilder->CreateLoad(loadType, ptr);
  trackLLVMValue(ret);

  llvm::MDNode* tbaa = NULL;
  if (USE_TBAA && valType &&
      (isClass(valType) || !valType->symbol->llvmTbaaStructCopyNode)) {
    if (surroundingStruct) {
      INT_ASSERT(fieldTbaaTypeDescriptor != info->tbaaRootNode);
      tbaa = info->mdBuilder->createTBAAStructTagNode(
               surroundingStruct->symbol->llvmTbaaAggTypeDescriptor,
               fieldTbaaTypeDescriptor, fieldOffset, isConst);
    } else {
      if (isConst) tbaa = valType->symbol->llvmConstTbaaAccessTag;
      else tbaa = valType->symbol->llvmTbaaAccessTag;
    }
  }

  if (isLoadOfLocalVar == false && !info->loopStack.empty()) {
    const auto &loopData = info->loopStack.back();
    if (loopData.markMemoryOps)
      ret->setMetadata("llvm.access.group", loopData.accessGroup);
  }

  if (tbaa)
    ret->setMetadata(llvm::LLVMContext::MD_tbaa, tbaa);
  if (aliasScope)
    ret->setMetadata(llvm::LLVMContext::MD_alias_scope, aliasScope);
  if (noalias)
    ret->setMetadata(llvm::LLVMContext::MD_noalias, noalias);
  return ret;
}

static
llvm::LoadInst* codegenLoadLLVM(GenRet ptr,
                                Type* valType = NULL,
                                bool isConst = false)
{
  if( ptr.chplType && !valType ) {
    if( ptr.isLVPtr ) valType = ptr.chplType;
    else valType = ptr.chplType->getValType();
  }

  return codegenLoadLLVM(ptr.val, valType, ptr.surroundingStruct,
                         ptr.fieldOffset, ptr.fieldTbaaTypeDescriptor,
                         ptr.aliasScope,
                         ptr.noalias,
                         isConst,
                         !ptr.mustPointOutsideOrderIndependentLoop);
}

#endif

GenRet codegenUseGlobal(const char* global)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if( info->cfile ) {
    ret.c = global;
  } else {
#ifdef HAVE_LLVM
    ret = info->lvt->getValue(global);
    if( ! ret.val )
      INT_FATAL("Could not find global %s, "
                "perhaps it is missing or is complicated macro?", global);
    assert( ret.isLVPtr != GEN_WIDE_PTR );
    if( ret.isLVPtr == GEN_PTR ) {
      ret.val = codegenLoadLLVM(ret);
    }
    INT_ASSERT(ret.val);
#endif
  }
  ret.isLVPtr = GEN_VAL;
  return ret;
}

static
GenRet codegenLocaleForNode(GenRet node)
{
  node = codegenValue(node);

  std::vector<GenRet> args;
  args.push_back(codegenValue(node));
  args.push_back(codegenUseGlobal("c_sublocid_none"));
  args.push_back(codegenZero());
  args.push_back(codegenZero32());

  auto baseBuildLocaleId = gChplBuildLocaleId->codegenAsCallBaseExpr();
  GenRet ret = codegenCallExprWithArgs(baseBuildLocaleId, args,
                                       gChplBuildLocaleId->cname,
                                       gChplBuildLocaleId, nullptr, true);
  ret.chplType = LOCALE_ID_TYPE;
  return ret;
}


static
GenRet codegenUseCid(Type* classType)
{
  std::string varname;
  varname = varname + "chpl__cid_" + classType->symbol->cname;
  GenRet ret = codegenUseGlobal(varname.c_str());
  ret.chplType = CLASS_ID_TYPE;
  return ret;
}


// A construct which gives the current node ID (int32_t).
static
GenRet codegenGetNodeID(void)
{
  GenRet ret =  codegenUseGlobal("chpl_nodeID");
  ret.chplType = NODE_ID_TYPE;
  return ret;
}

// A construct which gives the current locale ID.
GenRet codegenGetLocaleID(void)
{
  GenRet ret =  codegenCallExpr("chpl_gen_getLocaleID");
  ret.chplType = LOCALE_ID_TYPE;
#ifdef HAVE_LLVM
  GenInfo* info = gGenInfo;
  if (!info->cfile ) {
    // Make sure that the result of gen_getLocaleID is
    // the right type (since clang likes to fold int32/int32 into int64).
    GenRet expectType = LOCALE_ID_TYPE;
    ret.val = convertValueToType(ret.val, expectType.type, false, true);
    assert(ret.val);
  }
#endif
  return ret;
}


static
GenRet codegenUseGlobal(std::string str)
{
  return codegenUseGlobal(str.c_str());
}

static
GenRet codegenWideHere(GenRet addr, Type* wideType = NULL)
{
  GenRet locale = codegenGetLocaleID();
  GenRet addrVal = codegenValue(addr);
  GenRet ret = codegenWideAddr(locale, addrVal, wideType);
  return ret;
}

static bool isWide(GenRet x)
{
  if( x.isLVPtr == GEN_WIDE_PTR ) return true;
  if( x.chplType && x.chplType->isWidePtrType() ) return true;
  return false;
}

// This function takes in something already code-generated that should be some
// sort of wide thing (isLVPtr == GEN_WIDE_PTR, or wide ref or wide class).
// It returns the local reference type and sets *wideRefTypeOut
// to the wide reference type.
static
Type* getRefTypesForWideThing(GenRet wide, Type** wideRefTypeOut)
{
  Type* ret = NULL;
  Type* wideRefType = NULL;
  if( wide.chplType ) {
    // Set the resulting Chapel type.
    if( wide.isLVPtr == GEN_WIDE_PTR ) {
      // wide lv-pointer, e.g. to int,
      // so we return a reference to int.
      ret = getOrMakeRefTypeDuringCodegen(wide.chplType);
      wideRefType = getOrMakeWideTypeDuringCodegen(ret);
    } else {
      // local lv-pointer or value; in such cases they are wide
      // only if they are a wide reference or a wide class.
      // Then the wide type is the current Chapel type.
      if( wide.chplType->symbol->hasEitherFlag(FLAG_WIDE_REF,FLAG_WIDE_CLASS) ) {
        ret = wide.chplType->getField("addr")->typeInfo();
        wideRefType = wide.chplType;
      } else {
        INT_ASSERT(0); // Not a wide thing.
      }
    }
  }
  if( wideRefTypeOut ) *wideRefTypeOut = wideRefType;
  return ret;
}

// This function casts a wide pointer to a void* wide pointer (ie wide_ptr_t)
// for use with packed wide pointers.
/*
static GenRet codegenCastWideToVoid(GenRet wide) {

  INT_ASSERT(wide.isLVPtr == GEN_WIDE_PTR ||
             (wide.chplType &&
              wide.chplType->symbol->hasEitherFlag(FLAG_WIDE_REF,FLAG_WIDE_CLASS)));

  // If we have a local pointer to a wide reference,
  // codegen value it.
  if( wide.isLVPtr == GEN_PTR ) {
    wide = codegenValue(wide);
  }

  // If we don't already have a wide reference - that is, if
  // wide.isLVPtr == GEN_WIDE_PTR - convert it to a Chapel reference in order
  // to create a temporary variable and have fewer cases below.
  if( wide.isLVPtr == GEN_WIDE_PTR ) {
    wide = codegenAddrOf(wide);
    // The result is wide.isLVPtr == GEN_VAL but wide.chplType is a wide ref
  }

  return codegenCast("wide_ptr_t", wide);
}
*/

// Extract a field of a wide string/ptr, returning an lvalue-pointer to the that
// field if we have a pointer to the wide string/ptr.  We need this function
// because codegenRaddr and friends now work with void wide pointer data-types
// (and wide strings are not the same as other wide types), and because
// codegenFieldPtr doesn't work to extract wide string fields (since it thinks
// it's supposed to be extracting fields from the class rather than from the
// wide ptr).
//
// Works for wide strings or wide pointers.
//
// field is WIDE_GEP_LOC, WIDE_GEP_ADDR
static GenRet codegenWideThingField(GenRet ws, WideThingField field)
{
  GenRet ret;
  GenInfo* info = gGenInfo;
  Type* wideRefType = nullptr;
  Type* refType = nullptr;

  INT_ASSERT(field == WIDE_GEP_LOC ||
             field == WIDE_GEP_ADDR);

  refType = getRefTypesForWideThing(ws, &wideRefType);

  INT_ASSERT(wideRefType);

  if( field == WIDE_GEP_LOC ) {
    ret.chplType = LOCALE_ID_TYPE;
  } else if( field == WIDE_GEP_ADDR ) {
    // get the local reference type
    // this will probably be overwritten by the caller,
    // but it is used below in the LLVM code.
    ret.chplType = refType;
  }

  const char* fname = wide_fields[field];

  if( info->cfile ) {
    if (ws.isLVPtr == GEN_PTR) {
      ret.isLVPtr = GEN_PTR;
      ret.c += "&((" + ws.c + ")->" + fname + ")";
    } else {
      // This case handles GEN_WIDE_PTR or GEN_VAL
      // we don't have an l-value for this one.
      // Could be wide lv-ptr or GEN_VAL wide ref.
      ret.isLVPtr = GEN_PTR;
      ret.c += "&((" + ws.c + ")." + fname + ")";
    }
  } else {
#ifdef HAVE_LLVM
    if ( !fLLVMWideOpt ) {
      if (ws.val->getType()->isPointerTy()){
        ret.isLVPtr = GEN_PTR;
        GenRet genWideType = wideRefType;
        llvm::Type* ty = genWideType.type;
        INT_ASSERT(ty);
        ret.val = info->irBuilder->CreateConstInBoundsGEP2_32(
                                            ty, ws.val, 0, field);
        trackLLVMValue(ret.val);
      } else {
        ret.isLVPtr = GEN_VAL;
        ret.val = info->irBuilder->CreateExtractValue(ws.val, field);
        trackLLVMValue(ret.val);
      }
      assert(ret.val);
    } else {

      // Workaround: for LLVMWideOpt, get pointers to parts
      // of addresses, but only support that when they are rvalues.

      // TODO: replace this code with an assert.

      // It would probably be better to fix InsertWideReferences.
      // The problematic pattern comes up when the optimization
      //  local _array -> local _array._instance fires in the
      // array's deinit/_do_destroy code.
      if( field == WIDE_GEP_LOC ) {
        ret = createTempVarWith(codegenRlocale(ws));
      } else if( field == WIDE_GEP_ADDR ) {
        ret = createTempVarWith(codegenRaddr(ws));
      }
    }
#endif
  }

  return ret;
}

// Generates code to load the remote address from a wide address.
// Always returns the address portion of the wide pointer as a value.
// The .chplType of the result will be a reference or class type.
GenRet codegenRaddr(GenRet wide)
{
  GenRet ret;
  Type* wideRefType = NULL;
  Type* type = NULL;

  type = getRefTypesForWideThing(wide, &wideRefType);

  if( !fLLVMWideOpt ) {
    ret = codegenValue(codegenWideThingField(wide, WIDE_GEP_ADDR));
  } else {
#ifdef HAVE_LLVM
    GenInfo* info = gGenInfo;
    if (wide.isLVPtr == GEN_PTR) wide = codegenValue(wide);
    GenRet wideTy = wideRefType; // get the LLVM type for the wide ref.
    llvm::PointerType *addrType = llvm::cast<llvm::PointerType>(wideTy.type);

    // call GLOBAL_FN_GLOBAL_ADDR dummy function
    llvm::Function* fn = getAddrFn(info->module, &info->globalToWideInfo,
                                   addrType);
    INT_ASSERT(fn);
    ret.val = info->irBuilder->CreateCall(fn, wide.val);
    trackLLVMValue(ret.val);
#endif
    ret = codegenCast(type, ret);
  }
  ret.chplType = type;
  return ret;
}

// Generates code to load the remote locale from a wide address
static GenRet codegenRlocale(GenRet wide)
{
  GenRet ret;
  Type* type = LOCALE_ID_TYPE;

  if( !fLLVMWideOpt ) {
    ret = codegenWideThingField(wide, WIDE_GEP_LOC);
  } else {
#ifdef HAVE_LLVM
    Type* wideRefType = NULL;
    GenInfo* info = gGenInfo;
    getRefTypesForWideThing(wide, &wideRefType);
    if (wide.isLVPtr == GEN_PTR) wide = codegenValue(wide);
    GenRet wideTy = wideRefType; // get the LLVM type for the wide ref.
    llvm::PointerType *addrType = llvm::cast<llvm::PointerType>(wideTy.type);

    // call GLOBAL_FN_GLOBAL_LOCID dummy function
    llvm::Function* fn = getLocFn(info->module, &info->globalToWideInfo, addrType);
    INT_ASSERT(fn);
    ret.val = info->irBuilder->CreateCall(fn, wide.val);
    trackLLVMValue(ret.val);
#endif
  }
  ret.chplType = type;
  return ret;
}

static GenRet codegenRsubloc(GenRet wide){
  GenRet ret;
  Type* type = SUBLOC_ID_TYPE;

  if( !fLLVMWideOpt ) {
    ret = codegenCallExpr("chpl_sublocFromLocaleID",
                          codegenValue(
                              codegenWideThingField(wide, WIDE_GEP_LOC)),
                          /*ln*/codegenZero(), /*fn*/codegenZero32());
  } else {
    USR_FATAL("--llvm-wide-opt can't be used with the GPU locale model");
  }

  ret.chplType = type;
  return ret;
}

static GenRet codegenRnode(GenRet wide){
  GenRet ret;
  Type* type = NODE_ID_TYPE;

  if( !fLLVMWideOpt ) {
    ret = codegenCallExpr("chpl_nodeFromLocaleID",
                          codegenValue(
                              codegenWideThingField(wide, WIDE_GEP_LOC)),
                          /*ln*/codegenZero(), /*fn*/codegenZero32());
  } else {
#ifdef HAVE_LLVM
    Type* wideRefType = NULL;
    GenInfo* info = gGenInfo;
    getRefTypesForWideThing(wide, &wideRefType);
    if (wide.isLVPtr == GEN_PTR) wide = codegenValue(wide);
    GenRet wideTy = wideRefType; // get the LLVM type for the wide ref.
    llvm::PointerType *addrType = llvm::cast<llvm::PointerType>(wideTy.type);

    // call GLOBAL_FN_GLOBAL_NODEID dummy function
    llvm::Function* fn = getNodeFn(info->module, &info->globalToWideInfo, addrType);
    INT_ASSERT(fn);
    ret.val = info->irBuilder->CreateCall(fn, wide.val);
    trackLLVMValue(ret.val);
#endif
  }

  ret.chplType = type;
  return ret;
}

static const int field_normal = 0;
static const int field_cid = 1;
static const int field_uid = 2;
static const int field_other = 3;

// Generates code to produce a pointer to the member (ie a field).
// Does not normally do any loads,stores,puts,or gets;
//  just does address arithmetic. The exception is if it has
//  to read an lvalue or when we have a reference to a class.
//
// This function handles the following cases:
//  1 base.chplType is a Chapel class
//  2 base.chplType is a Chapel wide class
//  3 base.chplType is a Chapel record and base.isLVPtr is set
//  4 base.chplType is a Chapel union and base.isLVPtr is set
//  5 base.chplType is a Chapel reference or wide reference type to a record
//  6 base.chplType is a Chapel reference or wide reference type to a union
//  7 base.chplType is a Chapel reference or wide reference to
//    a class or wide class (* causes a load)
//
//  In addition, it handles some special cases which are not reflected
//   in the Chapel type system, like getting the class ID or union ID
//   fields.
//
//  In any case, returns a GEN_PTR or GEN_WIDE_PTR to the field.
//
//  This is equivalent to C (assuming x has ptr type e.g. struct mystruct*)
//    & x->myfield
//
static
GenRet doCodegenFieldPtr(
    GenRet base,
    const char *c_field_name,
    const char* chpl_field_name,
    int special /* field_normal,field_cid, or field_uid */ ) {
  GenInfo* info = gGenInfo;
  GenRet ret;
  Type* baseType = base.chplType;
  AggregateType* ct = NULL;
  Type* castType = NULL;

  INT_ASSERT(baseType);

  // Reduce the Chapel reference or wide reference cases
  // to GEN_PTR or GEN_WIDE_PTR cases.
  if (baseType->symbol->hasEitherFlag(FLAG_REF,FLAG_WIDE_REF)) {
    if (baseType->getValType()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
      base = codegenDeref(base);
      base = codegenValue(base);
    } else {
      base = codegenDeref(base);
    }
    return doCodegenFieldPtr(base, c_field_name, chpl_field_name, special);
  }

  if( ! fLLVMWideOpt ) {
    // Reduce GEN_WIDE_PTR or FLAG_WIDE_CLASS cases to local versions
    // and rebuild addresses.
    if( base.isLVPtr == GEN_WIDE_PTR ||
        (baseType && baseType->symbol->hasFlag(FLAG_WIDE_CLASS)) ) {
      GenRet addr;
      addr = codegenRaddr(base);
      addr = doCodegenFieldPtr(addr, c_field_name, chpl_field_name, special);
      ret = codegenWideAddrWithAddr(base, addr);
      return ret;
    }
  }

  // At this point, baseType should be a record, union, class, or wide class
  // All of these types are in the AggregateType AST node.
  ct = toAggregateType(baseType);
  INT_ASSERT(ct);

  if ( isClass(ct) ) {
    // ok, we have a class type. We should codegenValue
    // to make sure we have no extra indirection.
    base = codegenValue(base);
  } else if ( baseType->symbol->hasFlag(FLAG_WIDE_CLASS)) {
    // Get the local version of the class (because it has the fields)
    base = codegenValue(base);
    baseType = baseType->getField("addr")->typeInfo();
    ct = toAggregateType(baseType);
  } else {
    // Must be a record or union type, and we must have an
    // lvalue-ptr to one of them.
    INT_ASSERT(isRecord(ct) || isUnion(ct));
    INT_ASSERT( base.isLVPtr != GEN_VAL );
  }

  // No Chapel field name? it must be special.
  if( !chpl_field_name && !special ) special = field_other;

  if( special ) {
    if( special == field_cid ) {
      INT_ASSERT( ct && isClass(ct) );
      ret.chplType = CLASS_ID_TYPE;
      castType = dtObject;
    } else if( special == field_uid ) {
      ret.chplType = UNION_ID_TYPE;
    } else {
      ret.chplType = NULL;
    }
  } else if( ct ) {
    // The field might be in a base class, so we
    // cast to the right base class type. If the field
    // is in the class, there is no need to cast.
    Symbol* fieldSymbol = ct->getField(chpl_field_name);
    if( isClass(ct) ) {
      castType = fieldSymbol->defPoint->parentSymbol->typeInfo();
      if( castType == ct ) castType = NULL;
    }
    ret.chplType = fieldSymbol->type;
  }

  ret.isLVPtr = GEN_PTR;

  if (isClass(ct) ) {
    base = codegenValue(base);
  } else {
    // not a class. base is a lvalue pointer.
    if( !fLLVMWideOpt ) INT_ASSERT(base.isLVPtr == GEN_PTR);
    else INT_ASSERT(base.isLVPtr != GEN_VAL);
  }
  if( info->cfile ) {
    ret.c = '&';
    ret.c += "(";
    if( castType ) ret.c += codegenCast(castType,base).c;
    else ret.c += "(" + base.c + ")";
    ret.c += "->";
    if (isUnion(ct) && !ct->symbol->hasFlag(FLAG_EXTERN) && !special)
      ret.c += "_u.";
    ret.c += c_field_name;
    ret.c += ")";
  } else {
#ifdef HAVE_LLVM
    // LLVM codegen
    llvm::Value* baseValue = base.val;

    // with LLVMWideOpt, we might return a wide ptr.
    if( fLLVMWideOpt && isWide(base) ) ret.isLVPtr = GEN_WIDE_PTR;

    // cast if needed
    if (castType) {
      Type* useCastType = castType;
      if (fLLVMWideOpt && isWide(base))
        useCastType = getOrMakeWideTypeDuringCodegen(castType);

      llvm::Type* castTypeLLVM = useCastType->codegen().type;
      baseValue = convertValueToType(base.val, castTypeLLVM, !base.isUnsigned);
      INT_ASSERT(baseValue);
    }

    AggregateType *cBaseType = castType ? toAggregateType(castType) : ct;

    // Compute the LLVM type of the structure we are accessing
    llvm::Type* baseTy = cBaseType->symbol->getLLVMStructureType();
    INT_ASSERT(baseTy);

    // We need the LLVM type of the field we're getting
    INT_ASSERT(ret.chplType);
    GenRet retType = ret.chplType;

    if (isUnion(ct) && !ct->symbol->hasFlag(FLAG_EXTERN) && !special) {
      // Get a pointer to the union data then cast it to the right type
      bool unused;
      ret.val = info->irBuilder->CreateStructGEP(
          baseTy, baseValue, cBaseType->getMemberGEP("_u", unused));
      trackLLVMValue(ret.val);
      auto addrSpace = baseValue->getType()->getPointerAddressSpace();
      llvm::PointerType* ty = llvm::PointerType::get(retType.type, addrSpace);
      // Now cast it to the right type.
      ret.val = convertValueToType(ret.val, ty, false);
      INT_ASSERT(ret.val);
    } else {
      // Normally, we just use a GEP.
      bool isCArrayField = false;
      int fieldno = cBaseType->getMemberGEP(c_field_name, isCArrayField);
      if (isCArrayField &&
          ret.chplType->getValType()->symbol->hasFlag(FLAG_C_PTR_CLASS)) {
        // Accessing field that is a C array declared with c_ptr(eltType)
        // should result in a pointer to the first element.
        TypeSymbol* eltTypeSym =
          getDataClassType(ret.chplType->getValType()->symbol);
        GenRet genEltType = eltTypeSym->type;
        llvm::Type* retTy = genEltType.type;
        INT_ASSERT(retTy);
        ret.val = info->irBuilder->CreateStructGEP(baseTy, baseValue, fieldno);
        trackLLVMValue(ret.val);
        llvm::StructType* sTy = llvm::cast<llvm::StructType>(baseTy);
        INT_ASSERT(sTy);
        llvm::Type* eltTy = sTy->getElementType(fieldno);
        ret.val = info->irBuilder->CreateStructGEP(eltTy, ret.val, 0);
        ret.isLVPtr = GEN_VAL;
        trackLLVMValue(ret.val);
      } else {
        ret.val = info->irBuilder->CreateStructGEP(baseTy, baseValue, fieldno);
        trackLLVMValue(ret.val);

        if (isUnion(ct) ||
            // Workaround: since we don't store full function pointer types
            // for interop and instead rely on c_fn_ptr, we need to cast
            // an extern field access returning a pointer to c_fn_ptr
            // to keep the types consistent. This should be removed once
            // c_fn_ptr is replaced with something more accurate.
            (ret.chplType == dtCFnPtr)) {
          // cast the returned pointer to the right type
          auto addrSpace = baseValue->getType()->getPointerAddressSpace();
          llvm::PointerType* ty =
            llvm::PointerType::get(retType.type, addrSpace);
          ret.val = convertValueToType(ret.val, ty, false);
        }

        if ((isClass(ct) || isRecord(ct)) &&
          cBaseType->symbol->llvmTbaaAggTypeDescriptor &&
          ret.chplType->symbol->llvmTbaaTypeDescriptor != info->tbaaRootNode) {
          llvm::StructType *structType = llvm::cast<llvm::StructType>(baseTy);
          ret.surroundingStruct = cBaseType;
          ret.fieldOffset = info->module->getDataLayout().
            getStructLayout(structType)->getElementOffset(fieldno);
          ret.fieldTbaaTypeDescriptor =
            ret.chplType->symbol->llvmTbaaTypeDescriptor;
        }
      }
    }

    // Propagate noalias scopes
    if (base.aliasScope)
      ret.aliasScope = base.aliasScope;
    if (base.noalias)
      ret.noalias = base.noalias;

#endif
  }
  return ret;
}

static
GenRet codegenFieldPtr(GenRet base, Expr* field) {
  const char* cname = NULL;
  const char* name = NULL;

  GenRet genBase = base;
  if(DefExpr *de = toDefExpr(field)) {
    cname = de->sym->cname;
    name = de->sym->name;
  } else if(SymExpr *se = toSymExpr(field)) {
    cname = se->symbol()->cname;
    name = se->symbol()->name;
  } else if(NamedExpr *ne = toNamedExpr(field)) {
    cname = name = ne->name;
  } else {
    INT_FATAL("Unknown field in codegenFieldPtr");
  }

  return doCodegenFieldPtr(genBase, cname, name, field_normal);
}

static
GenRet codegenFieldCidPtr(GenRet base) {
  GenRet ret = doCodegenFieldPtr(base, "chpl__cid", NULL, field_cid);
  //if( ! ret.chplType ) ret.chplType = CLASS_ID_TYPE;
  return ret;
}

static
GenRet codegenFieldUidPtr(GenRet base) {
  GenRet ret = doCodegenFieldPtr(base, "_uid", NULL, field_uid);
  //if( ! ret.chplType ) ret.chplType = UNION_ID_TYPE;
  return ret;
}


// Generates code to produce a pointer an array element.
//
// Handles the following cases:
//  1 base.chplType is a data class (ie _ddata)
//  2 base.chplType is a wide data class
//  3 base.chplType is a homogeneous tuple (aka star tuple) and isLVPtr != 0
//  4 base.chplType is a Chapel reference or wide reference
//    to a data class, wide data class, or homogeneous tuple.
//
// In any case, returns a GEN_PTR or GEN_WIDE_PTR to the element.
//
// This is equivalent to C (assuming ptr is a pointer type)
//   ptr + i
// If ddataPtr is true, we return a pointer to the element.  This is
//  currently only used for the PRIM_ARRAY_SHIFT_BASE_POINTER case.
//
static
GenRet codegenElementPtr(GenRet base, GenRet index, bool ddataPtr=false) {
  GenRet ret;
  GenInfo* info = gGenInfo;
  Type* baseType = NULL;
  Type* baseValType = NULL;
  Type* eltType = NULL;
  std::string addr;
  bool isStarTuple = false;

  INT_ASSERT(base.chplType);

  // Handle references to arrays or star tuples
  // by converting them to isLVPtr != GEN_VAL
  if( base.chplType->symbol->hasEitherFlag(FLAG_REF,FLAG_WIDE_REF) ) {
    base = codegenDeref(base);
  }

  baseType = base.chplType;

  // Now we should either have:
  //  - wide data class
  //  - data class
  //  - star tuple with isLVPtr != 0

  if( ! fLLVMWideOpt ) {
    // Convert wide pointer operations to the local counterparts.
    if( base.isLVPtr == GEN_WIDE_PTR ||
        baseType->symbol->hasFlag(FLAG_WIDE_CLASS) ) {
      GenRet newAddr = codegenElementPtr(codegenRaddr(base), index, ddataPtr);
      if (ddataPtr) {
        GenRet ret = codegenWideAddrWithAddr(base, newAddr, baseType);
        // Tell the compiler this is a ddata pointer not a ref to an element
        ret.isLVPtr = GEN_PTR;
        ret.chplType = base.chplType;
        ret.isUnsigned = true;
        return ret;
      } else {
        return codegenWideAddrWithAddr(base, newAddr);
      }
    }
  }

  ret.isLVPtr = GEN_PTR;
  if( fLLVMWideOpt && isWide(base) ) ret.isLVPtr = GEN_WIDE_PTR;

  baseValType = baseType->getValType();
  if (baseValType->symbol->hasEitherFlag(FLAG_WIDE_REF,FLAG_WIDE_CLASS)) {
    // with --llvm-wide-opt we can get here e.g. for a wide _ddata
    // and in that case, we need to compute the element type from
    // within the regular (narrow) ddata
    baseValType = baseValType->getField("addr")->typeInfo();
  }

  if (baseValType->symbol->hasFlag(FLAG_STAR_TUPLE)) {
    eltType = baseValType->getField("x0")->typeInfo();
    isStarTuple = true;
    // Star tuples should only be passed by reference here...
    INT_ASSERT(base.isLVPtr != GEN_VAL);
  } else if (baseValType->symbol->hasFlag(FLAG_C_ARRAY)) {
    eltType = toAggregateType(baseValType)->cArrayElementType();
    isStarTuple = true;
  } else if (baseValType->symbol->hasFlag(FLAG_DATA_CLASS)) {
    eltType = getDataClassType(baseValType->symbol)->typeInfo();
    isStarTuple = false;
  }

  if (ddataPtr) {
    // Tell the compiler this is a ddata pointer not a ref to an element
    ret.chplType = baseType;
    ret.isUnsigned = true;
  } else {
    ret.chplType = eltType;
  }

  index = codegenValue(index);
  if( !isStarTuple ) base = codegenValue(base);

  if( info->cfile ) {
    base = codegenValue(base); // even for tuple, for style.
    ret.c = "(" + base.c + " + " + index.c + ")";
    if (baseType->symbol->hasFlag(FLAG_C_PTRCONST_CLASS)) {
      // For our const C pointer representation, we will codegen the type as
      // const, but want to use it as non-const most places in the generated C.
      // This is because the rest of code generation doesn't try to handle C
      // constness, and we don't want to have to add that support everywhere.
      // Here, we cast away the constness so this pointer can be assigned into
      // a non-const call tmp without issue, for dereferencing or other use.
      ret.c = "((void*)" + ret.c + ")";
    }
  } else {
#ifdef HAVE_LLVM
    unsigned AS = base.val->getType()->getPointerAddressSpace();

    // in LLVM, arrays are not pointers and cannot be used in
    // calls to CreateGEP, CreateCall, CreateStore, etc.
    // so references to arrays must be used instead
    // (i.e. if it is a reference to an array, do not deref)
    std::vector<llvm::Value *> GEPLocs;

    // add zero as first index if tuple
    if (isStarTuple){
      GEPLocs.push_back(
          llvm::Constant::getNullValue(
            llvm::IntegerType::getInt64Ty(info->module->getContext())));
    }
    GEPLocs.push_back(extendToPointerSize(index, AS));

    llvm::Type* gepBaseType = nullptr;
    if (baseValType->symbol->hasFlag(FLAG_DATA_CLASS)) {
      // tuples and c_array are stored as LLVM arrays but ddatas are pointers
      gepBaseType = eltType->codegen().type;
    } else {
      gepBaseType = baseValType->codegen().type;
    }
    ret.val = createInBoundsGEP(gepBaseType, base.val, GEPLocs);

    // Propagate noalias scopes
    if (base.aliasScope)
      ret.aliasScope = base.aliasScope;
    if (base.noalias)
      ret.noalias = base.noalias;

#endif
  }

  return ret;
}

// 'alignment' is expected to follow 'AlignmentStatus'
// 'alignment' is UNUSED for C codegen
GenRet createTempVar(const char* ctype, int alignment)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  char name[32];
  snprintf(name, sizeof(name), "chpl_macro_tmp_%d", codegen_tmp++);

  ret.isLVPtr = GEN_PTR;
  if( info->cfile ) {
    // Add a temporary variable
    info->cLocalDecls.push_back(std::string(ctype) + " " + name);
    ret.c = std::string("&") + name;
  } else {
#ifdef HAVE_LLVM
    bool isUnsigned;
    llvm::Type* llTy = info->lvt->getType(ctype, &isUnsigned);
    INT_ASSERT(llTy);
    ret.val = createVarLLVM(llTy, name, alignment);
    ret.isUnsigned = isUnsigned;
#endif
  }
  return ret;
}

// use this function for chplTypes
// the created temp obeys t's alignment
GenRet createTempVar(Type* t)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if( info->cfile ) {
    // Just use the C-name.
    ret = createTempVar(t->symbol->cname, 0); // alignment is unused here
  } else {
#ifdef HAVE_LLVM
    // We need to code-generate the type in the event
    // that it does not exist. That happens for some
    // types that are constructed during code generation
    // (to do with references and references pointers)
    // It's not a problem for C since the type will
    // be added to the header before the C compiler runs.
    // getLLVMType() code-generates the type, if needed.
    llvm::Type* llTy = t->getLLVMType();
    llvm::AllocaInst* alloca = createVarLLVM(llTy, t, (Symbol*)nullptr);
    ret.isLVPtr = GEN_PTR;
    ret.val = alloca;
#endif
  }
  ret.chplType = t;
  ret.isUnsigned = !is_signed(t);
  return ret;
}

GenRet createTempVarWith(GenRet v)
{
  GenInfo* info = gGenInfo;
  Type* t = v.chplType;
  INT_ASSERT(t);
  GenRet ret = createTempVar(t);
  ret.isUnsigned = v.isUnsigned;
  // now store into the temp var the value we have.
  if( info->cfile ) {
    std::string stmt = codegenValue(ret).c + " = " + codegenValue(v).c + ";\n";
    info->cStatements.push_back(stmt);
  } else {
#ifdef HAVE_LLVM
    codegenStoreLLVM(codegenValue(v), ret);
#endif
  }
  return ret;
}


// For C code generation
// Codegen *(ptr), but we optimize away
// & or &(something)
// if GenRet is a wide pointer, we will emit a 'get'.
// For a star tuple, if we have a reference to a star tuple,
// returns the base address.
GenRet codegenValue(GenRet r)
{
  GenInfo* info = gGenInfo;
  GenRet ret = r;
  ret.isLVPtr = GEN_VAL;

  if( r.isLVPtr == GEN_VAL ) return ret;
  if( r.isLVPtr == GEN_WIDE_PTR && !fLLVMWideOpt) {
    // with fLLVMWideOpt, we can just load directly below.
    assert(r.chplType);

    // Emit a temporary.
    // Assign from wide pointer value into temporary
    // Return local pointer to temporary
    ret = createTempVar(r.chplType);
    codegenAssign(ret, r);
    return codegenValue(ret);
  }

  // At this point r.isPtr == GEN_PTR.
  if( r.chplType ) {
    // If we have a Chapel type, propagate it.
    ret.chplType = r.chplType;
    // NOT value type if it's a reference, since
    // codegenValue on a Chapel reference just returns the pointer!
  }

  if( info->cfile ) {
    INT_ASSERT(r.c.length() > 0);
    if( r.c[0] == '&' ) {
      if( r.c[1] == '(' && r.c[r.c.length()-1] == ')' ) {
        // we have &(something)
        ret.c = r.c.substr(2, r.c.length()-3);
      } else {
        // we have &something
        ret.c = r.c.substr(1, r.c.length()-1);
      }
    } else if( r.c[0] == '(' && r.c[r.c.length()-1] == ')') {
      // we have (something)
      ret.c = "*" + r.c;
    } else {
      ret.c = "*(" + r.c + ")";
    }
  } else {
#ifdef HAVE_LLVM
    if (r.isLVPtr) {
      // But don't dereference star tuples (since C views these as arrays)
      if( r.chplType && r.chplType->symbol->hasFlag(FLAG_STAR_TUPLE) ) {
        ret.val = r.val;
        ret.isLVPtr = r.isLVPtr;
      } else ret.val = codegenLoadLLVM(r); // TODO - is r pointer to const?
    } else {
      ret.val = r.val;
    }
#endif
  }
  return ret;
}

// codegenValue r, but deref it first if necessary
GenRet codegenValueMaybeDeref(Expr* r) {
  GenRet ret;
  if (r->isRefOrWideRef()) {
    ret = codegenValue(codegenDeref(r));
  } else {
    ret = codegenValue(r);
  }
  return ret;
}

// Create a temporary value holding r and return a pointer to it.
// If r is already a pointer, do nothing.
// Does not handle homogeneous tuples.
// Does not handle wide pointers.
GenRet codegenValuePtr(GenRet r)
{
  GenRet ret = r;

  // In codegen, 'nil' has to be treated like literal value.  Specifically,
  // in remote puts, it has to be copied into a temporary first, and the address
  // of the temporary used as the local buffer address in chpl_gen_comm_put().
  if( ret.isLVPtr == GEN_PTR && r.chplType != dtNil)
    return ret;

  if( r.chplType ) {
    bool isStarTuple = r.chplType->symbol->hasFlag(FLAG_STAR_TUPLE);
    INT_ASSERT(!isStarTuple);
  }

  INT_ASSERT(r.isLVPtr != GEN_WIDE_PTR);
  ret = createTempVarWith(r);
  return ret;
}


// Converts an L-value pointer into a
// pointer value, so that it can for example
// be stored in another pointer.
static GenRet codegenAddrOf(GenRet r)
{
  GenRet ret = r;

  if (r.isLVPtr == GEN_WIDE_PTR) {
    if(r.chplType) {
      Type* refType = getOrMakeRefTypeDuringCodegen(r.chplType);
      ret.chplType = getOrMakeWideTypeDuringCodegen(refType);
    }
    ret.isLVPtr = GEN_VAL;
    return ret;
  } else if( r.isLVPtr == GEN_PTR ) {
    if(r.chplType) ret.chplType = getOrMakeRefTypeDuringCodegen(r.chplType);
    ret.isLVPtr = GEN_VAL;
  } else {
    INT_FATAL("misuse of codegenAddrOf");
  }

  return ret;
}
// Converts an L-value pointer into a
// pointer value, so that it can for example
// be stored in another pointer.
// If we start with a wide pointer, we just discard
// the locale portion (ie assume it is local).
static
GenRet codegenLocalAddrOf(GenRet r)
{
  if (r.isLVPtr == GEN_WIDE_PTR) {
    return codegenRaddr(r);
  }
  return codegenAddrOf(r);
}



GenRet codegenLocalDeref(GenRet r)
{
  GenRet ret;
  // LocalDeref on a wide pointer should just give
  // the address field as a reference.
  if( r.chplType && r.chplType->symbol->hasFlag(FLAG_WIDE_REF) ) {
    ret = codegenRaddr(r);
    return ret;
  }
  // For some reason, ArgSymbol might not have a real Chapel
  // reference type, so we have this function as a workaround
  // (instead of running codegenDeref with chplType=type->refType )
  ret = codegenValue(r);
  ret.isLVPtr = GEN_PTR;
  if( r.chplType ) ret.chplType = r.chplType->getValType();
  return ret;
}

// codegenValue(r) to remove & or add * (if & already removed) and sets isLVPtr
GenRet codegenDeref(GenRet r)
{
  GenRet ret;

  INT_ASSERT(r.chplType);
  if (r.chplType->symbol->hasEitherFlag(FLAG_WIDE_REF, FLAG_WIDE_CLASS)) {
    ret = codegenValue(r);
    ret.isLVPtr = GEN_WIDE_PTR;
    ret.chplType = r.chplType->getValType();
  } else if ( r.chplType->symbol->hasFlag(FLAG_REF) ){
    return codegenLocalDeref(r);
  } else {
    //when an svec member value is returned and it is actually an address
    INT_ASSERT(0); // not a reference.
  }

  return ret;
}

static
GenRet codegenEquals(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  ret.chplType = dtBool;
  if( info->cfile ) ret.c = "(" + av.c + " == " + bv.c + ")";
  else {
#ifdef HAVE_LLVM
   // if type mismatch, create cast on RHS
   if (av.val->getType() != bv.val->getType()){
     bv.val = convertValueToType(bv.val, av.val->getType(), !bv.isUnsigned);
     INT_ASSERT(bv.val);
   }
   if( av.val->getType()->isFPOrFPVectorTy() ) {
     ret.val = info->irBuilder->CreateFCmpOEQ(av.val, bv.val);
     trackLLVMValue(ret.val);
   } else {
     ret.val = info->irBuilder->CreateICmpEQ(av.val, bv.val);
     trackLLVMValue(ret.val);
   }
#endif
  }
  return ret;
}

static
GenRet codegenNotEquals(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  ret.chplType = dtBool;
  if( info->cfile ) ret.c = "(" + av.c + " != " + bv.c + ")";
  else {
#ifdef HAVE_LLVM
   // if type mismatch, create cast on RHS
   if (av.val->getType() != bv.val->getType()){
     bv.val = convertValueToType(bv.val, av.val->getType(), !bv.isUnsigned);
     INT_ASSERT(bv.val);
   }
   if( av.val->getType()->isFPOrFPVectorTy() ) {
     ret.val = info->irBuilder->CreateFCmpUNE(av.val, bv.val);
     trackLLVMValue(ret.val);
   } else {
     ret.val = info->irBuilder->CreateICmpNE(av.val, bv.val);
     trackLLVMValue(ret.val);
   }
#endif
  }
  return ret;
}

static
GenRet codegenLessEquals(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  ret.chplType = dtBool;
  if( info->cfile ) ret.c = "(" + av.c + " <= " + bv.c + ")";
  else {
#ifdef HAVE_LLVM
    PromotedPair values = convertValuesToLarger(
                                 av.val,
                                 bv.val,
                                 is_signed(av.chplType),
                                 is_signed(bv.chplType));

    if (values.a->getType()->isFPOrFPVectorTy()) {
      ret.val = gGenInfo->irBuilder->CreateFCmpOLE(values.a, values.b);
      trackLLVMValue(ret.val);

    } else if (!values.isSigned) {
      ret.val = gGenInfo->irBuilder->CreateICmpULE(values.a, values.b);
      trackLLVMValue(ret.val);

    } else {
      ret.val = gGenInfo->irBuilder->CreateICmpSLE(values.a, values.b);
      trackLLVMValue(ret.val);
    }
#endif
  }
  return ret;
}


static
GenRet codegenLogicalOr(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  ret.chplType = dtBool;
  if( info->cfile ) ret.c = "(" + av.c + " || " + bv.c + ")";
  else {
#ifdef HAVE_LLVM
    ret.val = info->irBuilder->CreateOr(info->irBuilder->CreateIsNotNull(av.val),
                                        info->irBuilder->CreateIsNotNull(bv.val));
    trackLLVMValue(ret.val);
#endif
  }
  return ret;
}
static
GenRet codegenLogicalAnd(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  ret.chplType = dtBool;
  if( info->cfile ) ret.c = "(" + av.c + " && " + bv.c + ")";
  else {
#ifdef HAVE_LLVM
    ret.val = info->irBuilder->CreateAnd(info->irBuilder->CreateIsNotNull(av.val),
                                         info->irBuilder->CreateIsNotNull(bv.val));
    trackLLVMValue(ret.val);
#endif
  }
  return ret;
}

static
GenRet codegenAdd(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  if( info->cfile ) ret.c = "(" + av.c + " + " + bv.c + ")";
  else {
#ifdef HAVE_LLVM
    bool a_signed = false;
    bool b_signed = false;
    if( av.chplType ) a_signed = is_signed(av.chplType);
    if( bv.chplType ) b_signed = is_signed(bv.chplType);

    if (av.chplType == dtComplex[COMPLEX_SIZE_64]) {
      ret = codegenCallExpr("complexAdd64", av, bv);
    } else if (av.chplType == dtComplex[COMPLEX_SIZE_128]) {
      ret = codegenCallExpr("complexAdd128", av, bv);
    } else if(av.val->getType()->isPointerTy() ||
              bv.val->getType()->isPointerTy()) {
      // Handle pointer arithmetic ( e.g. int8* + int64)
      // We must have one integer and one pointer, not two pointers.
      GenRet *ptr = NULL;
      GenRet *i = NULL;
      if(av.val->getType()->isPointerTy()) ptr = &av;
      else i = &av;
      if(bv.val->getType()->isPointerTy()) ptr = &bv;
      else i = &bv;

      // We must have a pointer and an integer.
      INT_ASSERT(ptr && i);

      unsigned AS = ptr->val->getType()->getPointerAddressSpace();

      INT_ASSERT(ptr->chplType &&
                 ptr->chplType->symbol->hasFlag(FLAG_DATA_CLASS));
      TypeSymbol* eltTypeSym = getDataClassType(ptr->chplType->symbol);
      GenRet genEltType = eltTypeSym->type;
      INT_ASSERT(genEltType.type);
      // Emit a GEP instruction to do the addition.
      ret.isUnsigned = true; // returning a pointer, consider them unsigned
      ret.val = createInBoundsGEP(genEltType.type, ptr->val,
                                  extendToPointerSize(*i, AS));
    } else {
      PromotedPair values =
        convertValuesToLarger(av.val, bv.val, a_signed, b_signed);
      if(values.a->getType()->isFPOrFPVectorTy()) {
        ret.val = info->irBuilder->CreateFAdd(values.a, values.b);
        trackLLVMValue(ret.val);
      } else {
        // Purpose of adding values.isSigned is to generate 'nsw' argument
        // to add instruction if addition happens to be between signed integers.
        // This causes overflowing on adding to be undefined behaviour as in C.
        ret.val = info->irBuilder->CreateAdd(values.a, values.b, "", false, values.isSigned);
        trackLLVMValue(ret.val);
      }
      ret.isUnsigned = !values.isSigned;
    }
#endif
  }
  return ret;
}

static
GenRet codegenSub(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  if( info->cfile ) ret.c = "(" + av.c + " - " + bv.c + ")";
  else {
#ifdef HAVE_LLVM
    bool a_signed = false;
    bool b_signed = false;
    if( av.chplType ) a_signed = is_signed(av.chplType);
    if( bv.chplType ) b_signed = is_signed(bv.chplType);

    if (av.chplType == dtComplex[COMPLEX_SIZE_64]) {
      ret = codegenCallExpr("complexSubtract64", av, bv);
    } else if (av.chplType == dtComplex[COMPLEX_SIZE_128]) {
      ret = codegenCallExpr("complexSubtract128", av, bv);
    } else if(av.val->getType()->isPointerTy()) {
      // Handle pointer arithmetic by calling codegenAdd
      // with a negative value.
      INT_ASSERT(bv.val->getType()->isIntegerTy());
      GenRet negbv;
      negbv.val = info->irBuilder->CreateNSWNeg(bv.val);
      negbv.isUnsigned = false;
      trackLLVMValue(negbv.val);
      ret = codegenAdd(av, negbv);
    } else {
      PromotedPair values =
        convertValuesToLarger(av.val, bv.val, a_signed, b_signed);
      if(values.a->getType()->isFPOrFPVectorTy()) {
        ret.val = info->irBuilder->CreateFSub(values.a, values.b);
        trackLLVMValue(ret.val);
      } else {
        ret.val = info->irBuilder->CreateSub(values.a, values.b, "", false, values.isSigned);
        trackLLVMValue(ret.val);
      }
      ret.isUnsigned = !values.isSigned;
    }
#endif
  }
  return ret;
}

static
GenRet codegenNeg(GenRet a)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  GenRet av = codegenValue(a);
  if( info->cfile ) ret.c = "(-" + av.c + ")";
  else {
#ifdef HAVE_LLVM
    llvm::Value *value = av.val;
    if (av.chplType == dtComplex[COMPLEX_SIZE_64]) {
      ret = codegenCallExpr("complexUnaryMinus64", av);
    } else if (av.chplType == dtComplex[COMPLEX_SIZE_128]) {
      ret = codegenCallExpr("complexUnaryMinus128", av);
    } else if(value->getType()->isFPOrFPVectorTy()) {
      ret.val = info->irBuilder->CreateFNeg(value);
      trackLLVMValue(ret.val);
    } else {
      bool av_signed = false;
      if(av.chplType) av_signed = is_signed(av.chplType);
#if HAVE_LLVM_VER >= 190
      ret.val = info->irBuilder->CreateNeg(value, "", /*NSW*/av_signed);
#else
      ret.val = info->irBuilder->CreateNeg(value, "", /*NUW*/false, /*NSW*/av_signed);
#endif
      trackLLVMValue(ret.val);
    }
    ret.isUnsigned = false;
#endif
  }
  return ret;
}


static
GenRet codegenMul(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  if( info->cfile ) ret.c = "(" + av.c + " * " + bv.c + ")";
  else {
#ifdef HAVE_LLVM
    bool a_signed = false;
    bool b_signed = false;
    if( av.chplType ) a_signed = is_signed(av.chplType);
    if( bv.chplType ) b_signed = is_signed(bv.chplType);
    if (av.chplType == dtComplex[COMPLEX_SIZE_64]) {
      ret = codegenCallExpr("complexMultiply64", av, bv);
    } else if (av.chplType == dtComplex[COMPLEX_SIZE_128]) {
      ret = codegenCallExpr("complexMultiply128", av, bv);
    } else {
      PromotedPair values =
        convertValuesToLarger(av.val, bv.val, a_signed, b_signed);
      if(values.a->getType()->isFPOrFPVectorTy()) {
        ret.val = info->irBuilder->CreateFMul(values.a, values.b);
        trackLLVMValue(ret.val);
      } else {
        ret.val = info->irBuilder->CreateMul(values.a, values.b, "", false, values.isSigned);
        trackLLVMValue(ret.val);
      }
      ret.isUnsigned = !values.isSigned;
    }
#endif
  }
  return ret;
}


static
GenRet codegenDiv(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  if( info->cfile ) ret.c = "(" + av.c + " / " + bv.c + ")";
  else {
#ifdef HAVE_LLVM
    if (av.chplType == dtComplex[COMPLEX_SIZE_64]) {
      ret = codegenCallExpr("complexDivide64", av, bv);
    } else if (av.chplType == dtComplex[COMPLEX_SIZE_128]) {
      ret = codegenCallExpr("complexDivide128", av, bv);
    } else {
      PromotedPair values =
        convertValuesToLarger(av.val, bv.val,
                              is_signed(av.chplType),
                              is_signed(bv.chplType));
      if(values.a->getType()->isFPOrFPVectorTy()) {
        ret.val = info->irBuilder->CreateFDiv(values.a, values.b);
        trackLLVMValue(ret.val);
      } else {
        if(!values.isSigned) {
          ret.val = info->irBuilder->CreateUDiv(values.a, values.b);
          trackLLVMValue(ret.val);
        } else {
          ret.val = info->irBuilder->CreateSDiv(values.a, values.b);
          trackLLVMValue(ret.val);
        }
      }
    }
#endif
  }
  return ret;
}

static
GenRet codegenMod(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  if( info->cfile ) ret.c = "(" + av.c + " % " + bv.c + ")";
  else {
#ifdef HAVE_LLVM
    PromotedPair values =
      convertValuesToLarger(av.val, bv.val,
                            is_signed(av.chplType),
                            is_signed(bv.chplType));
    if(values.a->getType()->isFPOrFPVectorTy()) {
      ret.val = info->irBuilder->CreateFRem(values.a, values.b);
      trackLLVMValue(ret.val);
    } else {
      if(!values.isSigned) {
        ret.val = info->irBuilder->CreateURem(values.a, values.b);
        trackLLVMValue(ret.val);
      } else {
        ret.val = info->irBuilder->CreateSRem(values.a, values.b);
        trackLLVMValue(ret.val);
      }
    }
#endif
  }
  return ret;
}

// TODO: We could call the C 'fma' function from 'math.h' here.
static GenRet emitFmaForC(GenRet av, GenRet bv, GenRet cv) {
  INT_FATAL("Should not reach here, user facing functions should call the "
            "appropriate C intrinsic in module code instead");
  GenRet ret;
  return ret;
}

static GenRet emitFmaForLlvm(GenRet av, GenRet bv, GenRet cv) {
  GenRet ret;
#ifdef HAVE_LLVM
  GenInfo* info = gGenInfo;
  INT_ASSERT(av.chplType == bv.chplType && bv.chplType == cv.chplType);
  INT_ASSERT(av.chplType == dtReal[FLOAT_SIZE_64] ||
             av.chplType == dtReal[FLOAT_SIZE_32]);
  auto ty = av.val->getType();
  INT_ASSERT(ty);

  if (!ty->isFPOrFPVectorTy()) {
    INT_FATAL("The FMA primitive can only evaluate floating point types!");
  }

  // The 'id' is the base intrinsic, and then 'tys' is used to mangle
  // the name, for example to create 'llvm.fma.64'. Since the types of
  // all arguments should be the same, we only need one type in 'tys'.
  auto id = llvm::Intrinsic::fma;
  std::vector<llvm::Type*> tys = { ty };
  std::vector<llvm::Value*> args = { av.val, bv.val, cv.val };
  ret.val = info->irBuilder->CreateIntrinsic(id, tys, args);
  trackLLVMValue(ret.val);
#endif

  return ret;
}

static GenRet codegenFma(GenRet a, GenRet b, GenRet c) {
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  if (c.chplType && c.chplType->symbol->isRefOrWideRef()) c = codegenDeref(c);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  GenRet cv = codegenValue(c);
  if (info->cfile) {
    ret = emitFmaForC(av, bv, cv);
  }
  else {
    ret = emitFmaForLlvm(av, bv, cv);
  }
  return ret;
}

static bool preferCMathOverLLVMIntr() {
  // prefer using the cmath implementation if using CHPL_GPU=nvidia for better
  // performance
  return usingGpuLocaleModel() &&
         getGpuCodegenType() == GpuCodegenType::GPU_CG_NVIDIA_CUDA;
}

static GenRet emitSqrtCMath(GenRet av) {
  GenRet ret;
  if (av.chplType == dtReal[FLOAT_SIZE_64]) {
    ret = codegenCallExpr("chpl_sqrt64", av);
  } else if (av.chplType == dtReal[FLOAT_SIZE_32]) {
    ret = codegenCallExpr("chpl_sqrt32", av);
  } else {
    INT_FATAL("The sqrt primitive can only evaluate floating point types!");
  }
  return ret;
}

static GenRet emitSqrtLLVMIntrinsic(GenRet av) {
  GenRet ret;
#ifdef HAVE_LLVM
  GenInfo* info = gGenInfo;
  INT_ASSERT(av.chplType == dtReal[FLOAT_SIZE_64] ||
             av.chplType == dtReal[FLOAT_SIZE_32]);
  auto ty = av.val->getType();
  INT_ASSERT(ty);

  if (!ty->isFPOrFPVectorTy()) {
    INT_FATAL("The sqrt primitive can only evaluate floating point types!");
  }
  auto id = llvm::Intrinsic::sqrt;
  std::vector<llvm::Type*> tys = { ty };
  std::vector<llvm::Value*> args = { av.val };
  ret.val = info->irBuilder->CreateIntrinsic(id, tys, args);
  trackLLVMValue(ret.val);
#endif

  return ret;
}

static GenRet codegenSqrt(GenRet a) {
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  GenRet av = codegenValue(a);
  if (info->cfile || preferCMathOverLLVMIntr()) {
    ret = emitSqrtCMath(av);
  } else {
    ret = emitSqrtLLVMIntrinsic(av);
  }
  return ret;
}

static GenRet emitAbsCMath(GenRet av) {
  GenRet ret;
  if (av.chplType == dtReal[FLOAT_SIZE_64]) {
    ret = codegenCallExpr("chpl_fabs64", av);
  } else if (av.chplType == dtReal[FLOAT_SIZE_32]) {
    ret = codegenCallExpr("chpl_fabs32", av);
  } else {
    INT_FATAL("The abs primitive can only evaluate floating point types!");
  }
  return ret;
}

static GenRet emitAbsLLVMIntrinsic(GenRet av) {
  GenRet ret;
#ifdef HAVE_LLVM
  GenInfo* info = gGenInfo;
  INT_ASSERT(av.chplType == dtReal[FLOAT_SIZE_64] ||
             av.chplType == dtReal[FLOAT_SIZE_32]);
  auto ty = av.val->getType();
  INT_ASSERT(ty);

  if (!ty->isFPOrFPVectorTy()) {
    INT_FATAL("The abs primitive can only evaluate floating point types!");
  }
  auto id = llvm::Intrinsic::fabs;
  std::vector<llvm::Type*> tys = { ty };
  std::vector<llvm::Value*> args = { av.val };
  ret.val = info->irBuilder->CreateIntrinsic(id, tys, args);
  trackLLVMValue(ret.val);
#endif

  return ret;
}

static GenRet codegenAbs(GenRet a) {
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  GenRet av = codegenValue(a);
  if (info->cfile || preferCMathOverLLVMIntr()) {
    ret = emitAbsCMath(av);
  } else {
    ret = emitAbsLLVMIntrinsic(av);
  }
  return ret;
}



static
GenRet codegenLsh(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  if( info->cfile ) ret.c = "(" + av.c + " << " + bv.c + ")";
  else {
#ifdef HAVE_LLVM
    llvm::Value* amt = convertValueToType(bv.val, av.val->getType(),
                                          is_signed(bv.chplType));
    bool av_signed = false;
    if(av.chplType) av_signed = is_signed(av.chplType);
    ret.val = info->irBuilder->CreateShl(av.val, amt, "", false, av_signed);
    trackLLVMValue(ret.val);
#endif
  }
  return ret;
}

static
GenRet codegenRsh(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  if( info->cfile ) ret.c = "(" + av.c + " >> " + bv.c + ")";
  else {

#ifdef HAVE_LLVM
    llvm::Value* amt = convertValueToType(bv.val, av.val->getType(),
                                          is_signed(bv.chplType));
    if(!is_signed(a.chplType)) {
      ret.val = info->irBuilder->CreateLShr(av.val, amt);
      trackLLVMValue(ret.val);
    } else {
      ret.val = info->irBuilder->CreateAShr(av.val, amt);
      trackLLVMValue(ret.val);
    }
#endif
  }
  return ret;
}

static
GenRet codegenAnd(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  if( info->cfile ) ret.c = "(" + av.c + " & " + bv.c + ")";
  else {
#ifdef HAVE_LLVM
    PromotedPair values =
      convertValuesToLarger(av.val, bv.val,
                            is_signed(av.chplType),
                            is_signed(bv.chplType));
    ret.val = info->irBuilder->CreateAnd(values.a, values.b);
    trackLLVMValue(ret.val);
#endif
  }
  return ret;
}

static
GenRet codegenOr(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  if( info->cfile ) ret.c = "(" + av.c + " | " + bv.c + ")";
  else {
#ifdef HAVE_LLVM
    PromotedPair values =
      convertValuesToLarger(av.val, bv.val,
                            is_signed(av.chplType),
                            is_signed(bv.chplType));
    ret.val = info->irBuilder->CreateOr(values.a, values.b);
    trackLLVMValue(ret.val);
#endif
  }
  return ret;
}

static
GenRet codegenXor(GenRet a, GenRet b)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
  if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);
  GenRet av = codegenValue(a);
  GenRet bv = codegenValue(b);
  if( info->cfile ) ret.c = "(" + av.c + " ^ " + bv.c + ")";
  else {
#ifdef HAVE_LLVM
    PromotedPair values =
      convertValuesToLarger(av.val, bv.val,
                            is_signed(av.chplType),
                            is_signed(bv.chplType));
    ret.val = info->irBuilder->CreateXor(values.a, values.b);
    trackLLVMValue(ret.val);
#endif
  }
  return ret;
}



static
GenRet codegenTernary(GenRet cond, GenRet ifTrue, GenRet ifFalse)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  Type* type = ifTrue.chplType;
  if( ! type ) type = ifFalse.chplType;
  ret.chplType = type;
#ifdef HAVE_LLVM
  bool ifTrueSigned = !ifTrue.isUnsigned;
  bool ifFalseSigned = !ifFalse.isUnsigned;
  if( ifTrue.chplType ) ifTrueSigned = is_signed(ifTrue.chplType);
  if( ifFalse.chplType ) ifFalseSigned = is_signed(ifFalse.chplType);
#endif

  if( info->cfile ) {
    ret.c = "( (" + cond.c + ")?(" + ifTrue.c + "):(" + ifFalse.c + ") )";
  } else {
#ifdef HAVE_LLVM
    llvm::Function *func = info->irBuilder->GetInsertBlock()->getParent();

    llvm::BasicBlock *blockIfTrue =llvm::BasicBlock::Create(
        info->module->getContext(), "ternaryBlockIfTrue");
    llvm::BasicBlock *blockIfFalse = llvm::BasicBlock::Create(
        info->module->getContext(), "ternaryBlockIfFalse");
    llvm::BasicBlock *blockEnd = llvm::BasicBlock::Create(
        info->module->getContext(), "ternaryBlockEnd");
    trackLLVMValue(blockIfTrue);
    trackLLVMValue(blockIfFalse);
    trackLLVMValue(blockEnd);

    GenRet ifTrueVal = codegenValue(ifTrue);
    GenRet ifFalseVal = codegenValue(ifFalse);
    PromotedPair values = convertValuesToLarger(
        ifTrueVal.val, ifFalseVal.val, ifTrueSigned, ifFalseSigned);

    char name[32];
    snprintf(name, sizeof(name), "chpl_macro_tmp_tv_%d", codegen_tmp++);

    llvm::Value* tmp = createVarLLVM(values.a->getType(), ret.chplType,
                                     (Symbol*)nullptr, name);

    llvm::BranchInst* condBr = info->irBuilder->CreateCondBr(
        codegenValue(cond).val, blockIfTrue, blockIfFalse);
    trackLLVMValue(condBr);

#if HAVE_LLVM_VER >= 160
    func->insert(func->end(), blockIfTrue);
#else
    func->getBasicBlockList().push_back(blockIfTrue);
#endif
    info->irBuilder->SetInsertPoint(blockIfTrue);
    llvm::StoreInst* store1 = info->irBuilder->CreateStore(values.a, tmp);
    trackLLVMValue(store1);
    llvm::BranchInst* toEnd1 = info->irBuilder->CreateBr(blockEnd);
    trackLLVMValue(toEnd1);

#if HAVE_LLVM_VER >= 160
    func->insert(func->end(), blockIfFalse);
#else
    func->getBasicBlockList().push_back(blockIfFalse);
#endif
    info->irBuilder->SetInsertPoint(blockIfFalse);
    llvm::StoreInst* store2 = info->irBuilder->CreateStore(values.b, tmp);
    trackLLVMValue(store2);
    llvm::BranchInst* toEnd2 = info->irBuilder->CreateBr(blockEnd);
    trackLLVMValue(toEnd2);

#if HAVE_LLVM_VER >= 160
    func->insert(func->end(), blockEnd);
#else
    func->getBasicBlockList().push_back(blockEnd);
#endif
    info->irBuilder->SetInsertPoint(blockEnd);
    ret.val = info->irBuilder->CreateLoad(ifTrue.chplType->symbol->getLLVMType(), tmp);
    trackLLVMValue(ret.val);

    ret.isUnsigned = !values.isSigned;
#endif
  }
  return ret;
}

// AKA == null
static
GenRet codegenIsZero(GenRet x)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (x.chplType->symbol->hasEitherFlag(FLAG_WIDE_REF,FLAG_WIDE_CLASS) ) {
    x = codegenRaddr(x);
    if (info->cfile) {
      ret.c = x.c;
      ret.c += " == nil";
    } else {
#ifdef HAVE_LLVM
      ret.val = info->irBuilder->CreateIsNull(x.val);
      trackLLVMValue(ret.val);
#endif
    }
  } else {
    GenRet xv = codegenValue(x);
    if( info->cfile ) ret.c = "(! " + xv.c + ")";
    else {
#ifdef HAVE_LLVM
      ret.val = info->irBuilder->CreateIsNull(xv.val);
      trackLLVMValue(ret.val);
#endif
    }
  }

  return ret;
}

// AKA != null
static
GenRet codegenIsNotZero(GenRet x)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (x.chplType->symbol->hasEitherFlag(FLAG_WIDE_REF,FLAG_WIDE_CLASS) ) {
    x = codegenRaddr(x);
    if (info->cfile) {
      ret.c = x.c;
      ret.c += " != nil";
    } else {
#ifdef HAVE_LLVM
      ret.val = info->irBuilder->CreateIsNotNull(x.val);
      trackLLVMValue(ret.val);
#endif
    }
  } else {
    GenRet xv = codegenValue(x);
    if( info->cfile ) ret.c = "(!(! " + xv.c + "))";
    else {
#ifdef HAVE_LLVM
      ret.val = info->irBuilder->CreateIsNotNull(xv.val);
      trackLLVMValue(ret.val);
#endif
    }
  }
  return ret;
}

static
GenRet codegenGlobalArrayElement(const char* table_name,
                                 const char* eltTypeName,
                                 GenRet elt)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  if (info->cfile) {
    ret.c = table_name;
    ret.c += "[";
    ret.c += elt.c;
    ret.c += "]";
  } else {
#ifdef HAVE_LLVM
    GenRet       table = info->lvt->getValue(table_name);

    INT_ASSERT(table.val);
    INT_ASSERT(elt.val);

    auto global = llvm::cast<llvm::GlobalVariable>(table.val);
    INT_ASSERT(global);
    GenRet eltTy = codegenTypeByName(eltTypeName);
    INT_ASSERT(eltTy.type);

    llvm::Value* GEPLocs[2];
    GEPLocs[0] = llvm::Constant::getNullValue(
        llvm::IntegerType::getInt64Ty(info->module->getContext()));
    GEPLocs[1] = extendToPointerSize(elt, 0);

    llvm::Value* elementPtr;
    elementPtr = createInBoundsGEP(global->getValueType(), table.val, GEPLocs);

    llvm::Instruction* element =
      info->irBuilder->CreateLoad(eltTy.type, elementPtr);
    trackLLVMValue(element);

    // I don't think it matters, but we could provide TBAA metadata
    // here to indicate global constant variable loads are constant...
    // I'd expect LLVM to figure that out because the table loaded is
    // constant.

    ret.val = element;
#endif
  }
  return ret;
}

// cid_Td is the class-id field value of the dynamic type
// Type* C is the type to downcast to
static
GenRet codegenDynamicCastCheck(GenRet cid_Td, Type* C)
{
  // see genSubclassArrays in codegen.cpp
  // currently using Schubert Numbering method
  //
  // Td is a subclass of C (or a C) iff
  //   n1(C) <= n1(Td) && n1(Td) <= n2(C)
  //
  // but note, we n1(C) *is* C->classId

  AggregateType* at = toAggregateType(C);
  INT_ASSERT(at != NULL);


  GenRet cid_C = codegenUseCid(C);
  GenRet n1_C = cid_C;
  // Since we use n1_Td twice, put it into a temp var
  // other than that, n1_Td is cid_Td.
  GenRet n1_Td = createTempVarWith(cid_Td);
  GenRet n2_C  = codegenGlobalArrayElement("chpl_subclass_max_id",
                                           "chpl__class_id", cid_C);

  GenRet part1 = codegenLessEquals(n1_C, n1_Td);
  GenRet part2 = codegenLessEquals(n1_Td, n2_C);

  GenRet ret = codegenLogicalAnd(part1, part2);
  return ret;
}

static
GenRet codegenArgForFormal(GenRet arg,
                           const FunctionType::Formal* formal,
                           bool defaultToValues,
                           bool isExternOrExport) {
  // NOTE -- VMT call had add & if arg isRecord.
  if (formal) {
    bool passRef = false;
    bool passWideRef = false;

    // We need to pass a reference in these cases
    // Don't pass a reference to extern/export functions
    // Do if argRequiresCPtr or the argument is of reference type
    if (isExternOrExport &&
        (!(formal->intent() & INTENT_FLAG_REF) ||
         formal->type()->getValType()->symbol->hasFlag(FLAG_TUPLE))) {
      // Don't pass by reference to extern functions
    } else if (argRequiresCPtr(formal) ||
               formal->qualType().isRef() ||
               formal->qualType().isWideRef()) {
      // Pass by reference in this case
      passRef = true;
      // If it's wide, make a note of it
      if (formal->qualType().isWideRef()) {
        passWideRef = true;
      }
    }

    // Make sure that the formal type + intent
    // matches, so we don't get multiple reference levels.

    // If we need to pass a reference but we already have a reference,
    // pass the pointer by value.
    if (arg.chplType && passRef) {
      if (passWideRef && arg.chplType->symbol->hasFlag(FLAG_WIDE_REF)) {
        passWideRef = false;
        passRef = false;
      }

      if (passRef && arg.chplType->symbol->hasFlag(FLAG_REF)) {
        passRef = false;
      }
    }

    if (passWideRef) {
      if( arg.isLVPtr == GEN_VAL ) {
        arg = codegenValuePtr(arg);
        arg = codegenWideHere(codegenAddrOf(arg));
      } else if( arg.isLVPtr == GEN_PTR ) {
        arg = codegenWideHere(codegenAddrOf(arg));
      } // otherwise, arg.isLVPtr == GEN_WIDE_PTR, no action necessary
    } else if(passRef) {
      if( arg.isLVPtr == GEN_VAL ) {
        arg = codegenValuePtr(arg);
      } else if( arg.isLVPtr == GEN_WIDE_PTR ) {
        arg = codegenValuePtr(codegenRaddr(arg));
      } // otherwise, arg.isLVPtr == GEN_PTR, no action necessary
    } else {
      if( arg.isLVPtr != GEN_VAL ) {
        arg = codegenValue(arg);
      }
    }
  } else {
    if( defaultToValues ) {
      if( arg.isLVPtr != GEN_VAL ) {
        arg = codegenValue(arg);
      }
    } // otherwise, leave it be.
  }
  return arg;
}

// if fn is non-NULL, we use that to decide what to dereference.
// Otherwise, if defaultToValues=true, we will codegenValue() the arguments,
//            and if it is false, they will pass by reference if they
//            are references.
static GenRet codegenCallExprInner(GenRet function,
                                   std::vector<GenRet> & args,
                                   FnSymbol* fn,
                                   ClangFunctionDeclPtr FD,
                                   LlvmFunctionTypePtr fnTyArg,
                                   bool defaultToValues) {
  bool isIndirect = !fn && !FD && isFunctionType(function.chplType);
  FunctionType* chplFnType = nullptr;
  GenInfo* info = gGenInfo;
  GenRet ret;

  // Fetch the computed Chapel function type.
  if (isIndirect) {
    chplFnType = toFunctionType(function.chplType);
  } else if (fn) {
    chplFnType = fn->computeAndSetType();
  }

  bool isExternOrExport = chplFnType && (chplFnType->isExtern() ||
                                         chplFnType->isExport());
  INT_ASSERT(!chplFnType || !chplFnType->throws());

  // As a first step, adjust the formals to have the proper types.
  for (size_t i = 0; i < args.size(); i++) {
    auto formal = chplFnType ? chplFnType->formal(i) : nullptr;
    args[i] = codegenArgForFormal(args[i], formal,
                                  defaultToValues,
                                  isExternOrExport);
  }

  // C
  if (info->cfile) {
    ret.c = function.c;
    ret.c += '(';
    bool first_actual = true;
    for (size_t i = 0; i < args.size(); i++) {
      if (first_actual) {
        first_actual = false;
      } else {
        ret.c += ", ";
      }
      ret.c += args[i].c;
    }
    ret.c += ')';

  // LLVM
  } else {
#ifdef HAVE_LLVM
    // See clang CodeGenFunction::EmitCall
    // See Swift irgen::emitForeignParameter
    llvm::IRBuilder<>* irBuilder = info->irBuilder;
    const llvm::DataLayout& layout = info->module->getDataLayout();
    llvm::LLVMContext &ctx = gContext->llvmContext();
    unsigned int stackSpace = layout.getAllocaAddrSpace();
    const clang::CodeGen::CGFunctionInfo* CGI = NULL;

    // Get the clang ABI info.
    if (isExternOrExport) {
      if (fn) {
        CGI = &getClangABIInfo(fn);
      } else if (chplFnType) {
        CGI = &getClangABIInfo(chplFnType);
      }
      INT_ASSERT(CGI != NULL);
    } else if (FD != NULL) {
      CGI = &getClangABIInfoFD(FD);
    }

    INT_ASSERT(function.val);
    llvm::Value* val = function.val;
    // Maybe function is bit-cast to a pointer?
    llvm::Function* func = llvm::dyn_cast<llvm::Function>(val);
    llvm::FunctionType* fnType = nullptr;

    if (func) {
      fnType = func->getFunctionType();
    } else if (fn) {
      GenRet t = fn->codegenFunctionType(false);
      fnType = llvm::dyn_cast<llvm::FunctionType>(t.type);
      INT_ASSERT(fnType);
    } else if (fnTyArg) {
      fnType = fnTyArg;
    } else if (chplFnType) {
      const auto& info = fetchLocalFunctionTypeLlvm(chplFnType);
      fnType = info.type;
      INT_ASSERT(isIndirect);

    } else {
      INT_FATAL("Could not compute called function type");
    }

    std::vector<llvm::Value *> llArgs;
    llvm::AllocaInst* sret = NULL;
    llvm::Type* chapelRetTy = NULL;
    bool chplRetTySigned = false;
    int retAlignment = ALIGNMENT_DEFER;

    if (chplFnType) {
      auto retType = chplFnType->returnType();
      if (retType == dtNothing || retType == dtVoid) {
        chapelRetTy = llvm::Type::getVoidTy(ctx);
      } else {
        chapelRetTy = retType->codegen().type;
        chplRetTySigned = is_signed(retType);
        retAlignment = retType->getLLVMAlignment();
      }
    } else if (FD) {
      clang::QualType retTy = FD->getCallResultType();
      if (retTy->isVoidType()) {
        chapelRetTy = llvm::Type::getVoidTy(ctx);
      } else {
        chapelRetTy = codegenCType(retTy);
        chplRetTySigned = retTy->hasSignedIntegerRepresentation();
        retAlignment = getCTypeAlignment(retTy);
      }
    }

    if (CGI == nullptr && fnType != nullptr &&
        fnType->getReturnType()->isVoidTy() &&
        fnType->getNumParams() >= 1 &&
        func && func->hasStructRetAttr())
      INT_FATAL("structure return without ABI info not implemented");

    if (CGI) {
      // Handle return ABI stuff
      const clang::CodeGen::ABIArgInfo& returnInfo = CGI->getReturnInfo();
      returnInfo.canHaveCoerceToType();

      if (CGI->getArgStruct() != NULL)
        INT_FATAL("inalloca arguments not yet implemented");

      if (returnInfo.isIndirect()) {
        // Create a temporary for holding the return value
        sret = createVarLLVM(chapelRetTy, "chpl_ret_temp", retAlignment);
        llArgs.push_back(sret);
      }
    }

    ArgSymbol* formal = NULL;
    if (fn && fn->numFormals() > 0)
      formal = fn->getFormal(1);

    for (size_t i = 0; i < args.size(); i++) {
      const clang::CodeGen::ABIArgInfo* argInfo = NULL;
      if (CGI) {
        argInfo = getCGArgInfo(CGI, i, fn);
      } else if (args[i].isLVPtr == GEN_VAL && useDarwinArmFix(args[i].chplType)) {
        argInfo = getSingleCGArgInfo(args[i].chplType);
      }

      if (argInfo) {
        if (llvm::Type* paddingTy = argInfo->getPaddingType()) {
          // Emit padding argument
          llArgs.push_back(llvm::UndefValue::get(paddingTy));
        }

        switch (argInfo->getKind()) {
          case clang::CodeGen::ABIArgInfo::Kind::Ignore:
            break;

          case clang::CodeGen::ABIArgInfo::Kind::InAlloca:
            INT_FATAL("inalloca arguments not yet implemented");
            break;

          case clang::CodeGen::ABIArgInfo::Kind::IndirectAliased:
            INT_FATAL("IndirectAliased not yet implemented");
            break;

          case clang::CodeGen::ABIArgInfo::Kind::Indirect:
          {
            // clang's CodeGenFunction::EmitCall contains many
            // optimizations for this case.
            GenRet arg = codegenAddrOf(codegenValuePtr(args[i]));
            llArgs.push_back(arg.val);
            break;
          }

          case clang::CodeGen::ABIArgInfo::Kind::Extend:
          case clang::CodeGen::ABIArgInfo::Kind::Direct:
          {
            llvm::Type* chapelArgTy = args[i].val->getType();
            if (chapelArgTy == NULL && formal != NULL)
              chapelArgTy = formal->type->codegen().type;

            if (!llvm::isa<llvm::StructType>(argInfo->getCoerceToType()) &&
                argInfo->getCoerceToType() == chapelArgTy &&
                argInfo->getDirectOffset() == 0) {
              // The simpler case
              llvm::Value* val = args[i].val;
              val = convertValueToType(val, argInfo->getCoerceToType(), true);
              llArgs.push_back(val);
            } else {
              // handle a more complex direct argument
              // (possibly in multiple registers)

              llvm::StructType *sTy = llvm::dyn_cast<llvm::StructType>(argInfo->getCoerceToType());

              if (argInfo->isDirect() && argInfo->getCanBeFlattened() && sTy &&
                  sTy->getNumElements() > 1) {

                // handle a complex direct argument with multiple registers

                GenRet tmp = args[i];
                tmp.val = convertValueToType(tmp.val, sTy, false, true);

                // Create a temp variable to load from
                tmp = createTempVarWith(args[i]);

                llvm::Value* ptr = tmp.val;
                llvm::Type* sTyPtrTy = llvm::PointerType::get(sTy, stackSpace);
                llvm::Type* i8PtrTy = getPointerType(irBuilder);

                // handle offset
                if (unsigned offset = argInfo->getDirectOffset()) {
                  ptr = irBuilder->CreatePointerCast(ptr, i8PtrTy);
                  trackLLVMValue(ptr);
                  ptr = irBuilder->CreateConstInBoundsGEP1_32(i8PtrTy, ptr, offset);
                  trackLLVMValue(ptr);
                }
                ptr = irBuilder->CreatePointerCast(ptr, sTyPtrTy);
                trackLLVMValue(ptr);

                unsigned nElts = sTy->getNumElements();
                for (unsigned i = 0; i < nElts; i++) {
                  // load to produce the next LLVM argument
                  llvm::Value* eltPtr =
                    irBuilder->CreateStructGEP(sTy, ptr, i);
                  trackLLVMValue(eltPtr);
                  llvm::Value* loaded =
                    irBuilder->CreateLoad(sTy->getElementType(i), eltPtr);
                  trackLLVMValue(loaded);
                  llArgs.push_back(loaded);
                }
              } else {
                // mainly just needing to convert the type
                GenRet tmp = args[i];
                llvm::Value* val = tmp.val;

                val = convertValueToType(val, argInfo->getCoerceToType(),
                                         !tmp.isUnsigned, true);
                llArgs.push_back(val);
              }
            }
            break;
          }

          case clang::CodeGen::ABIArgInfo::Kind::CoerceAndExpand:
          {
            llvm::StructType *sTy = argInfo->getCoerceAndExpandType();

            GenRet tmp = args[i];
            tmp.val = convertValueToType(tmp.val, sTy, false, true);

            // Create a temp variable to load from
            tmp = createTempVarWith(args[i]);

            llvm::Type* sTyPtrTy = llvm::PointerType::get(sTy, stackSpace);
            llvm::Value* ptr = irBuilder->CreatePointerCast(tmp.val, sTyPtrTy);
            trackLLVMValue(ptr);

            unsigned nElts = sTy->getNumElements();
            for (unsigned i = 0; i < nElts; i++) {
              llvm::Type *ty = sTy->getElementType(i);
              if (clang::CodeGen::ABIArgInfo::isPaddingForCoerceAndExpand(ty))
                continue;

              // load to produce the next LLVM argument
              llvm::Value* eltPtr = irBuilder->CreateStructGEP(sTy, ptr, i);
              trackLLVMValue(eltPtr);
              llvm::Value* loaded = irBuilder->CreateLoad(ty, eltPtr);
              trackLLVMValue(loaded);
              llArgs.push_back(loaded);
            }
            break;
          }

          case clang::CodeGen::ABIArgInfo::Kind::Expand:
            INT_FATAL("not implemented");
            break;
        }
      } else {

        if (func && fnType && llArgs.size() < fnType->getNumParams()) {
          bool funcHasAttribute =
            func->getAttributes().hasAttributeAtIndex(llArgs.size()+1,
                                                      llvm::Attribute::ByVal);
          if (funcHasAttribute)
            INT_FATAL("byval without ABI info not implemented");
        }
        llvm::Value* val = NULL;

        if (fnType && llArgs.size() < fnType->getNumParams()) {
          bool isSigned = !args[i].isUnsigned ||
                          (args[i].chplType && is_signed(args[i].chplType));
          llvm::Type* targetType = NULL;
          targetType = fnType->getParamType(llArgs.size());
          val = convertValueToType(args[i].val, targetType, isSigned, false);
          INT_ASSERT(val != NULL);
        } else {
          val = args[i].val;
        }

        llArgs.push_back(val);
      }

      if (formal)
        formal = next_formal(formal);
    }

    llvm::CallInst* c = NULL;

    if (func) {
      c = info->irBuilder->CreateCall(func, llArgs);
      trackLLVMValue(c);
    } else {
      if (!fnType) INT_FATAL("Could not compute called function type");
      c = info->irBuilder->CreateCall(fnType, val, llArgs);
      trackLLVMValue(c);
    }

    if (func) {
      // Add attributes to the call. These attributes can be critical
      // for getting ABI-sensitive code to compile correctly.
      llvm::AttributeList attrs = func->getAttributes();

      // Here we would remove any attributes on the function
      // that are not appropriate for the call.
      c->setAttributes(attrs);
    } else if (chplFnType) {
      const auto& info = fetchLocalFunctionTypeLlvm(chplFnType);
      c->setAttributes(info.attrs);
    }

    // we might add attributes for the call site only, e.g. NoBuiltin, here.

    ret.val = c;

    if( sret ) {
      ret.val = codegenLoadLLVM(sret, fn?(fn->retType):(NULL));
    }

    if (chapelRetTy && ret.val->getType() != chapelRetTy) {
      ret.val = convertValueToType(ret.val, chapelRetTy,
                                   chplRetTySigned, true);
    }

#endif
  }
  return ret;
}

static GenRet codegenCallExprWithArgs(GenRet function,
                                      std::vector<GenRet> & args,
                                      const char* fnName,
                                      FnSymbol* fnSym,
                                      ClangFunctionDeclPtr FD,
                                      bool defaultToValues) {
  GenInfo* info = gGenInfo;
  bool isIndirect = !fnSym && isFunctionType(function.chplType);

  INT_ASSERT(isIndirect || (fnName && fnName[0] != '\0'));

  // make sure fnSym and FD are set if they can be
  if (!isIndirect && fnSym == nullptr) {
    auto search = info->functionCNameAstrToSymbol.find(astr(fnName));
    if (search != info->functionCNameAstrToSymbol.end()) {
      fnSym = search->second;
    }
  }

  #ifdef HAVE_LLVM
    if (info->cfile == nullptr) {
      if (!isIndirect && !FD) {
        FD = getFunctionDeclClang(fnName);
      }
    }
  #endif


  // FD or fn should be set for LLVM code generation
  if (info->cfile == nullptr && !isIndirect && fnSym == nullptr && !FD) {
    INT_FATAL("Could not find FD or fn in codegenCallExprWithArgs");
  }

  return codegenCallExprInner(function, args, fnSym, FD, nullptr,
                              defaultToValues);
}

GenRet codegenCallExprWithArgs(const char* fnName,
                               std::vector<GenRet> & args,
                               FnSymbol* fnSym,
                               ClangFunctionDeclPtr FD,
                               bool defaultToValues)
{
  GenInfo* info = gGenInfo;
  GenRet fn;
  if( info->cfile ) {
    fn.c = fnName;
    return codegenCallExprWithArgs(fn, args, fnName,
                                   fnSym, FD, defaultToValues);
  } else {
#ifdef HAVE_LLVM
    fn.val = getFunctionLLVM(fnName);
    if (fn.val == nullptr) {
      INT_FATAL(fnSym, "unable to find function %s\n", fnName);
    }
    return codegenCallExprWithArgs(fn, args, fnName,
                                   fnSym, FD, defaultToValues);
#endif
  }

  INT_FATAL("should not be reached");
  return fn;
}

static
void codegenCallWithArgs(const char* fnName,
                         std::vector<GenRet> & args,
                         FnSymbol* fnSym,
                         ClangFunctionDeclPtr FD,
                         bool defaultToValues)
{
  GenInfo* info = gGenInfo;
  GenRet r = codegenCallExprWithArgs(fnName, args, fnSym, FD, defaultToValues);
  if( info->cfile ) {
    info->cStatements.push_back(r.c + ";\n");
  }
}

/* These overloads of codegenCall are a bit boring-looking,
 * but they make it much easier to write the primitive call
 * generation in Expr::codegen
 */
GenRet codegenCallExpr(const char* fnName)
{
  std::vector<GenRet> args;
  return codegenCallExprWithArgs(fnName, args);
}
GenRet codegenCallExpr(const char* fnName, GenRet a1)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  return codegenCallExprWithArgs(fnName, args);
}
GenRet codegenCallExpr(const char* fnName, GenRet a1, GenRet a2)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  args.push_back(a2);
  return codegenCallExprWithArgs(fnName, args);
}
static
GenRet codegenCallExpr(const char* fnName, GenRet a1, GenRet a2, GenRet a3)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  args.push_back(a2);
  args.push_back(a3);
  return codegenCallExprWithArgs(fnName, args);
}

/* static
void codegenCall(const char* fnName)
{
  std::vector<GenRet> args;
  codegenCallWithArgs(fnName, args);
}*/
static
void codegenCall(const char* fnName, GenRet a1)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  codegenCallWithArgs(fnName, args);
}
static
void codegenCall(const char* fnName, GenRet a1, GenRet a2)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  args.push_back(a2);
  codegenCallWithArgs(fnName, args);
}
static
void codegenCall(const char* fnName, GenRet a1, GenRet a2, GenRet a3)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  args.push_back(a2);
  args.push_back(a3);
  codegenCallWithArgs(fnName, args);
}
static
void codegenCall(const char* fnName, GenRet a1, GenRet a2, GenRet a3, GenRet a4)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  args.push_back(a2);
  args.push_back(a3);
  args.push_back(a4);
  codegenCallWithArgs(fnName, args);
}

static
void codegenCall(const char* fnName, GenRet a1, GenRet a2, GenRet a3,
                 GenRet a4, GenRet a5)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  args.push_back(a2);
  args.push_back(a3);
  args.push_back(a4);
  args.push_back(a5);
  codegenCallWithArgs(fnName, args);
}

static
void codegenCall(const char* fnName, GenRet a1, GenRet a2, GenRet a3,
                 GenRet a4, GenRet a5, GenRet a6)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  args.push_back(a2);
  args.push_back(a3);
  args.push_back(a4);
  args.push_back(a5);
  args.push_back(a6);
  codegenCallWithArgs(fnName, args);
}

static
void codegenCall(const char* fnName, GenRet a1, GenRet a2, GenRet a3,
                 GenRet a4, GenRet a5, GenRet a6, GenRet a7)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  args.push_back(a2);
  args.push_back(a3);
  args.push_back(a4);
  args.push_back(a5);
  args.push_back(a6);
  args.push_back(a7);
  codegenCallWithArgs(fnName, args);
}

static
void codegenCall(const char* fnName, GenRet a1, GenRet a2, GenRet a3,
                 GenRet a4, GenRet a5, GenRet a6, GenRet a7, GenRet a8)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  args.push_back(a2);
  args.push_back(a3);
  args.push_back(a4);
  args.push_back(a5);
  args.push_back(a6);
  args.push_back(a7);
  args.push_back(a8);
  codegenCallWithArgs(fnName, args);
}

/*
static
void codegenCall(const char* fnName, GenRet a1, GenRet a2, GenRet a3,
                 GenRet a4, GenRet a5, GenRet a6, GenRet a7, GenRet a8,
                 GenRet a9)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  args.push_back(a2);
  args.push_back(a3);
  args.push_back(a4);
  args.push_back(a5);
  args.push_back(a6);
  args.push_back(a7);
  args.push_back(a8);
  args.push_back(a9);
  codegenCallWithArgs(fnName, args);
}
*/

/*static
void codegenCall(const char* fnName, GenRet a1, GenRet a2, GenRet a3,
                 GenRet a4, GenRet a5, GenRet a6, GenRet a7, GenRet a8,
                 GenRet a9, GenRet a10)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  args.push_back(a2);
  args.push_back(a3);
  args.push_back(a4);
  args.push_back(a5);
  args.push_back(a6);
  args.push_back(a7);
  args.push_back(a8);
  args.push_back(a9);
  args.push_back(a10);
  codegenCallWithArgs(fnName, args);
}*/

/*
static
void codegenCall(const char* fnName, GenRet a1, GenRet a2, GenRet a3,
                 GenRet a4, GenRet a5, GenRet a6, GenRet a7, GenRet a8,
                 GenRet a9, GenRet a10, GenRet a11)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  args.push_back(a2);
  args.push_back(a3);
  args.push_back(a4);
  args.push_back(a5);
  args.push_back(a6);
  args.push_back(a7);
  args.push_back(a8);
  args.push_back(a9);
  args.push_back(a10);
  args.push_back(a11);
  codegenCallWithArgs(fnName, args);
}
*/

/*
static
void codegenCall(const char* fnName, GenRet a1, GenRet a2, GenRet a3,
                 GenRet a4, GenRet a5, GenRet a6, GenRet a7, GenRet a8,
                 GenRet a9, GenRet a10, GenRet a11, GenRet a12)
{
  std::vector<GenRet> args;
  args.push_back(a1);
  args.push_back(a2);
  args.push_back(a3);
  args.push_back(a4);
  args.push_back(a5);
  args.push_back(a6);
  args.push_back(a7);
  args.push_back(a8);
  args.push_back(a9);
  args.push_back(a10);
  args.push_back(a11);
  args.push_back(a12);
  codegenCallWithArgs(fnName, args);
}
*/

static
GenRet codegenZero()
{
  return new_IntSymbol(0, INT_SIZE_64)->codegen();
}

static
GenRet codegenZero32()
{
  return new_IntSymbol(0, INT_SIZE_32)->codegen();
}

/*
static
GenRet codegenOne()
{
  return new_IntSymbol(1, INT_SIZE_64)->codegen();
}
*/

GenRet codegenNullPointer()
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  ret.chplType = dtNil;
  if( info->cfile ) {
    ret.c = "NULL";
  } else {
#ifdef HAVE_LLVM
    llvm::Type* ptrType = getPointerType(info->irBuilder);
    ret.val = llvm::Constant::getNullValue(ptrType);
#endif
  }
  return ret;
}

// Create a memcpy call copying size bytes from src to dest.
// If we are copying a single type known to the compiler (e.g. a whole
// record), pointedToType can contain the single element type. If we
// are copying (or possibly copying) multiple elements, pointedToType
// should be NULL. pointedToType is used to emit alias analysis information.
static
void codegenCallMemcpy(GenRet dest, GenRet src, GenRet size,
                       Type* pointedToType) {
  GenInfo *info = gGenInfo;

  // Must call with two real pointer arguments
  //  (and not any lvalues)
  INT_ASSERT(dest.isLVPtr == GEN_VAL);
  INT_ASSERT(src.isLVPtr == GEN_VAL);

  if( info->cfile ) {
    codegenCall("memcpy", dest, src, size);
  } else {
#ifdef HAVE_LLVM

    // Caller should use e.g. codegenAddrOf, this function does
    // not operate on lvalues.
    dest = codegenValue(dest);
    src = codegenValue(src);
    size = codegenValue(size);

    llvm::Type *int8Ty = llvm::Type::getInt8Ty(gContext->llvmContext());
    llvm::Type *types[3];
    unsigned addrSpaceDest = llvm::cast<llvm::PointerType>(dest.val->getType())->getAddressSpace();
    unsigned addrSpaceSrc = llvm::cast<llvm::PointerType>(src.val->getType())->getAddressSpace();
    types[0] = llvm::PointerType::get(int8Ty, addrSpaceDest);
    types[1] = llvm::PointerType::get(int8Ty, addrSpaceSrc);
    types[2] = llvm::Type::getInt64Ty(gContext->llvmContext());
    //types[3] = llvm::Type::getInt32Ty(info->llvmContext);
    //types[4] = llvm::Type::getInt1Ty(info->llvmContext);

#if LLVM_VERSION_MAJOR >= 20
    llvm::Function *func =
      llvm::Intrinsic::getOrInsertDeclaration(
        info->module,
        llvm::Intrinsic::memcpy,
        types
      );
#else
    llvm::Function *func =
      llvm::Intrinsic::getDeclaration(
        info->module,
        llvm::Intrinsic::memcpy,
        types
      );
#endif
    //llvm::FunctionType *fnType = func->getFunctionType();

    // LLVM 7 and later: memcpy has no alignment argument
    llvm::Value* llArgs[4];

    llArgs[0] = convertValueToType(dest.val, types[0], false);
    llArgs[1] = convertValueToType(src.val, types[1], false);
    llArgs[2] = convertValueToType(size.val, types[2], false);

    // LLVM memcpy intrinsic has additional argument isvolatile
    // isVolatile?
    llArgs[3] = llvm::ConstantInt::get(llvm::Type::getInt1Ty(info->module->getContext()), 0, false);

    // We can't use IRBuilder::CreateMemCpy because that adds
    //  a cast to i8 (in address space 0).
    llvm::CallInst* CI = info->irBuilder->CreateCall(func, llArgs);
    trackLLVMValue(CI);

    llvm::MDNode* tbaaStructTag = NULL;
    if( pointedToType ) {
      tbaaStructTag = pointedToType->symbol->llvmTbaaStructCopyNode;
    }
    // LLVM's memcpy only supports tbaa.struct metadata, not scalar tbaa.
    // The reason is that LLVM is only looking for holes in structs here,
    // not aliasing.  Clang also puts only tbaa.struct on memcpy calls.
    // Adding scalar tbaa metadata here causes incorrect code to be generated.
    if( tbaaStructTag )
      CI->setMetadata(llvm::LLVMContext::MD_tbaa_struct, tbaaStructTag);
#endif
  }
}
#ifdef HAVE_LLVM

static
llvm::Constant* codegenSizeofLLVM(llvm::Type* type)
{
  // This used to use llvm::ConstantExpr::getSizeOf(type);
  // but that seems not to be constant folded.

  GenInfo *info = gGenInfo;
  const llvm::DataLayout& dl = info->module->getDataLayout();
  llvm::LLVMContext& ctx = info->module->getContext();

  INT_ASSERT(type->isSized());
  llvm::TypeSize ret = dl.getTypeAllocSize(type);
#if HAVE_LLVM_VER >= 160
  auto intValue = ret.getKnownMinValue();
#else
  auto intValue = ret.getKnownMinSize();
#endif
  llvm::Type* sizeTy = dl.getIntPtrType(ctx);

  return llvm::ConstantInt::get(sizeTy, intValue);
}

static
GenRet codegenSizeof(llvm::Type* type)
{
  GenRet ret;
  ret.chplType = SIZE_TYPE;
  ret.val = codegenSizeofLLVM(type);
  return ret;
}
#endif

static
GenRet codegenSizeof(const char* name)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  ret.chplType = SIZE_TYPE;
  if( info->cfile ) {
    ret.c = "sizeof(";
    ret.c += name;
    ret.c += ')';
  } else {
#ifdef HAVE_LLVM
    ret.val = codegenSizeofLLVM(getTypeLLVM(name));
#endif
  }
  return ret;
}


static
GenRet codegenSizeof(Type* t)
{
  return codegenSizeof(t->symbol->cname);
}

// dest dest must have isLVPtr set. This function implements
// part of codegenAssign.
static
void codegenCopy(GenRet dest, GenRet src, Type* chplType=NULL)
{
  assert( dest.isLVPtr );

  if( ! chplType ) chplType = src.chplType;
  if( ! chplType ) chplType = dest.chplType;

  // This does handle isLVPtr being wide but only
  // if we're using fLLVMWideOpt.
  if( ! fLLVMWideOpt ) {
    assert( dest.isLVPtr != GEN_WIDE_PTR );
    assert( src.isLVPtr != GEN_WIDE_PTR );
  }

#ifdef HAVE_LLVM
  GenInfo *info = gGenInfo;
  if( ! info->cfile ) {
    bool useMemcpy = false;

    if( chplType && chplType->symbol->llvmTbaaStructCopyNode ) {
      // Always use memcpy for things for which we've developed LLVM
      // struct nodes for alias analysis, since as far as we know, we
      // can't use tbaa.struct for load/store.
      useMemcpy = true;
    } else if( src.isLVPtr ) {
      // Consider using memcpy instead of stack allocating a possibly
      // large structure.

      llvm::Type* eltTy = nullptr;

      if (chplType) {
        eltTy = chplType->codegen().type;
      }

      if( chplType && chplType->symbol->hasFlag(FLAG_STAR_TUPLE) ) {
        // Always use memcpy for star tuples.
        useMemcpy = true;
      } else if( eltTy && isTypeSizeSmallerThan(info->module->getDataLayout(),
                                       eltTy,
                                       256 /* max bytes to load/store */)) {
        // OK
      } else {
        useMemcpy = true;
      }
    }

    if( !useMemcpy ) {
      // Do it with store(load(src_ptr), dst_ptr)
      src = codegenValue(src);
      codegenStoreLLVM(src, dest);
      return;
    }
  }
#endif

  // Otherwise call memcpy.
  dest = codegenAddrOf(dest);
  if( ! src.isLVPtr ) src = codegenValuePtr(src);
  src = codegenAddrOf(src);
  GenRet size = codegenSizeof(chplType);
  codegenCallMemcpy(dest, src, size, chplType);
}

static bool
isTupleOfTuple(BaseAST *e) {
  return (e->typeInfo()->symbol->hasFlag(FLAG_STAR_TUPLE) &&
          toDefExpr(toAggregateType(e->typeInfo())->fields.head)->sym->
               type->symbol->hasFlag(FLAG_TUPLE));
}


// MPF - perhaps this should always call codegenValue
// and return isLVPtr = GEN_VAL.
GenRet codegenCast(Type* t, GenRet value, bool Cparens)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  ret.chplType = t;
  ret.isLVPtr = value.isLVPtr;
  ret.isUnsigned = ! is_signed(t);

  // If we are casting to bool, set it to != 0.
  if( is_bool_type(t) ) {
    // NOTE: We have to limit this special treatment for bool cast to
    // C backend compilations. LLVM bool operations return single bit
    // integers whereas bool type is 8-bits. So we still need explicit
    // cast. Engin
    if(info->cfile) {
      return codegenNotEquals(value, codegenZero());
    } else {
#ifdef HAVE_LLVM
      GenRet notZero = codegenNotEquals(value, codegenZero());

      // convert the i1 result into an i8 (or appropriate bool width)
      llvm::Type* castType = t->codegen().type;
      ret.val = convertValueToType(notZero.val, castType, !ret.isUnsigned);
      INT_ASSERT(ret.val);
      return ret;
#endif
    }
  }

  // if we are casting a C99 wide pointer, parens around the value
  // will result in an error, hence the Cparens parameter
  // e.g. ((chpl____wide_DefaultRectangularArr_locale_1_int64_t_F)(
  //         { .locale = chpl_nodeID, .addr = nil }))
  // won't compile

  if (info->cfile){
    ret.c = "((" + t->codegen().c + ")";
    if (Cparens){
      ret.c += "(";
    }
    ret.c += value.c;
    if (Cparens){
      ret.c += ")";
    }
    ret.c += ")";
  } else {
#ifdef HAVE_LLVM
    llvm::Type* castType = t->codegen().type;

    ret.val = convertValueToType(value.val, castType, !value.isUnsigned);
    INT_ASSERT(ret.val);
#endif
  }
  return ret;
}


GenRet codegenCast(const char* typeName, GenRet value, bool Cparens)
{
  GenInfo* info = gGenInfo;
  GenRet ret;
  ret.isLVPtr = value.isLVPtr;
  ret.chplType = getNamedTypeDuringCodegen(typeName);

  if( info->cfile ) {
    ret.c = "((";
    ret.c += typeName;
    ret.c += ")";

    if (Cparens){
      ret.c += "(";
    }
    ret.c += value.c;
    if (Cparens){
      ret.c += ")";
    }
    ret.c += ")";

    /*ret.c = "((";
    ret.c += typeName;
    ret.c += ")(";
    ret.c += value.c;
    ret.c += "))";*/
  } else {
#ifdef HAVE_LLVM
    GenRet really = codegenValue(value);
    llvm::Type* castType = getTypeLLVM(typeName);
    ret.val = convertValueToType(really.val, castType, !really.isUnsigned);
    INT_ASSERT(ret.val);
#endif
  }
  return ret;
}

static
GenRet codegenCastToVoidStar(GenRet value)
{
  GenInfo* info = gGenInfo;
  GenRet ret;

  if( info->cfile ) {
    ret.c = "((void*)(";
    ret.c += value.c;
    ret.c += "))";
  } else {
#ifdef HAVE_LLVM
    llvm::Type* castType = getPointerType(info->irBuilder);
    ret.val = convertValueToType(value.val, castType, !value.isUnsigned);
    INT_ASSERT(ret.val);
#endif
  }
  return ret;
}

void codegenCallPrintf(const char* arg) {
  GenInfo* info = gGenInfo;

  if (info->cfile) {
    fprintf(info->cfile, "printf(\"%%s\", \"%s\");\n", arg);
  } else {
#ifdef HAVE_LLVM
    codegenCall("printf", new_CStringSymbol("%s")->codegen(), new_CStringSymbol(arg)->codegen());
#endif
  }
}

/* Commented out because it is not currently used.

static
GenRet codegenCastPtrToInt(Type* toType, GenRet value)
{
  GenInfo* info = gGenInfo;

  if( info->cfile ) {
    return codegenCast(toType, value);
  } else {
    GenRet ret;
#ifdef HAVE_LLVM
    llvm::Type* castType = toType->codegen().type;

    ret.val = info->irBuilder->CreatePtrToInt(value.val, castType);
    trackLLVMValue(ret.val);
    ret.isLVPtr = GEN_VAL;
    ret.chplType = toType;
    ret.isUnsigned = ! is_signed(toType);
#endif
    return ret;
  }
}
*/

// Generates code to perform an "assignment" operation, given
//  a destination pointer and a value.
// That's basically
//  (*)to_ptr = (*)from
// but for a homogeneous tuple, we will copy element-by-element
// or possibly call memcpy (in order to copy more that the first element).
//
// If to_ptr or from_ptr is a wide reference type (but not both),
// we will generate a PUT or a GET.
//
// from_type is used (in C) to create a temporary in case that
// is needed.
//
// This function will always copy some value. If that is not
// desired, other functions should be used.
//
// This function should have lvalues according to what should
// be copied; that is to_ptr should have isLVPtr set, and from
// can optionally have isLVPtr set.
static
void codegenAssign(GenRet to_ptr, GenRet from)
{
  GenInfo* info = gGenInfo;

  // To must be a pointer.
  INT_ASSERT(to_ptr.isLVPtr);

  Type* type = to_ptr.chplType;
  if( ! type ) type = from.chplType;
  INT_ASSERT(type);

  bool isStarTuple = false;
  int starTupleLength = 0;

  if (type->symbol->hasFlag(FLAG_STAR_TUPLE)) {
    isStarTuple = true;
    starTupleLength = toAggregateType(type)->fields.length;
  } else if (type->symbol->hasFlag(FLAG_C_ARRAY)) {
    isStarTuple = true;
    int64_t sizeInt = toAggregateType(type)->cArrayLength();
    if (sizeInt > INT_MAX)
      USR_FATAL(type->symbol->instantiationPoint, "c_array is too large");
    starTupleLength = (int) sizeInt;
  }

  // if from is a wide ptr a ref to dtNil, set from to
  // a nil pointer of the correct type.
  if (from.chplType && to_ptr.chplType){
    AggregateType* ct = toAggregateType(from.chplType);
    if (ct && ct->symbol->hasEitherFlag(FLAG_WIDE_REF, FLAG_WIDE_CLASS)) {
      Symbol* valField = ct->getField("addr");
      if (valField && valField->getValType() == dtNil) {
         from = codegenAddrOf(
             codegenWideHere(codegenNullPointer(), to_ptr.chplType));
      }
    }
    if (from.chplType == dtNil)
    {
      if (to_ptr.chplType->symbol->hasEitherFlag(FLAG_WIDE_REF, FLAG_WIDE_CLASS))
      {
        from = codegenWideHere(codegenNullPointer(), to_ptr.chplType);
        type = to_ptr.chplType->getValType();
        from.isLVPtr = GEN_VAL;
      } else {
        from = codegenNullPointer();
        type = to_ptr.chplType->getValType();
        from.isLVPtr = GEN_VAL;
      }
    }
  }

  if( (to_ptr.isLVPtr != GEN_WIDE_PTR && from.isLVPtr != GEN_WIDE_PTR )) {
    // Neither is wide.
    if( isStarTuple ) {
      // Homogeneous tuples are pointers even when GEN_VAL is set.
      // Homogeneous tuples are copied specially
      if ( !fNoTupleCopyOpt &&
           starTupleLength <= tuple_copy_limit &&
           !isTupleOfTuple(type) ) {
          // tuple copy optimization
          for (int i = 0; i < starTupleLength; i++) {
            GenRet to_i =
              codegenElementPtr(to_ptr, new_IntSymbol(i, INT_SIZE_64));
            GenRet from_i =
              codegenElementPtr(from, new_IntSymbol(i, INT_SIZE_64));
            codegenAssign(to_i, from_i);
          }
      } else {
        // tuple copy but use memcpy
        // to is already a pointer.
        codegenCopy(to_ptr, from, type);
      }
    } else {
      // not a homogeneous tuple copy
      if( info->cfile ) {
        GenRet value = codegenValue(from);
        INT_ASSERT(value.c != "");
        std::string stmt = codegenValue(to_ptr).c + " = ";
        stmt += value.c;
        stmt += ";\n";
        info->cStatements.push_back(stmt);
      } else {
#ifdef HAVE_LLVM
        // LLVM codegen assignment (non-wide, non-tuple)
        assert(from.val);
        GenRet value = codegenValue(from);
        assert(value.val);

        codegenStoreLLVM(value, to_ptr, type);
#endif
      }
    }
  } else {

    if (from.isLVPtr == GEN_WIDE_PTR && to_ptr.isLVPtr == GEN_WIDE_PTR){
      // Assign two wide pointers through a temporary.

      // Create a temporary, assign tmp = from,
      // then assign to = tmp.
      INT_ASSERT(from.chplType);

      GenRet tmp = createTempVar(from.chplType);
      codegenAssign(tmp, from);
      // Now assign to_ptr = tmp
      codegenAssign(to_ptr, tmp);
      return;
    }

    // One of the types is a wide pointer type, so we have to
    // call get or put.
    if( from.isLVPtr == GEN_WIDE_PTR ) { // GET
      INT_ASSERT(type);
      // would also be nice to call createTempVarWith to
      // store a temporary wide pointer so we don't get
      // code like:
      //  chpl_comm_get(...,
      //        ((wide type) {.locale = ..., .addr = ...}).locale,
      //        ((wide type) {.locale = ..., .addr = ...}).addr,
      //         ...);

      // Generate a GET
      if (forceWidePtrsForLocal()) {
        // We're actually doing local compilation, so just do a copy
        codegenCopy(to_ptr, codegenDeref(codegenRaddr(from)), type);
      } else {
        if (fLLVMWideOpt) {
          // LLVM pass will translate it to a get after some opts
          // We already know to is a pointer (wide or not).
          // Make sure that from is a pointer
          codegenCopy(to_ptr, from, type);
        } else {
          std::vector<GenRet> args;
          std::string fn;
          if (usingGpuLocaleModel()) {
            fn = "chpl_gen_comm_get_from_subloc";
          } else {
            fn = "chpl_gen_comm_get";
          }

          args.push_back(codegenCastToVoidStar(to_ptr));
          args.push_back(codegenRnode(from));
          if (usingGpuLocaleModel()) args.push_back(codegenRsubloc(from));
          args.push_back(codegenRaddr(from));
          args.push_back(codegenSizeof(type));
          args.push_back(genCommID(info));
          args.push_back(info->lineno);
          args.push_back(gFilenameLookupCache[info->filename]);

          codegenCallWithArgs(fn.c_str(), args);
        }
      }
    } else { // PUT
      if (forceWidePtrsForLocal()) {
        // We're actually doing local compilation, so just do a copy
        codegenCopy(codegenDeref(codegenRaddr(to_ptr)), from, type);
      } else {
        // Generate a PUT
        // to is already a pointer.
        if (fLLVMWideOpt) {
          // LLVM pass will translate it to a put after some opts
          // We already know to is a pointer (wide or not).
          // Make sure that from is a pointer
          codegenCopy(to_ptr, from, type);
        } else {
          std::vector<GenRet> args;
          std::string fn;
          if (usingGpuLocaleModel()) {
            fn = "chpl_gen_comm_put_to_subloc";
          } else {
            fn = "chpl_gen_comm_put";
          }

          args.push_back(codegenCastToVoidStar(codegenValuePtr(from)));
          args.push_back(codegenRnode(to_ptr));
          if (usingGpuLocaleModel()) args.push_back(codegenRsubloc(to_ptr));
          args.push_back(codegenRaddr(to_ptr));
          args.push_back(codegenSizeof(type));
          args.push_back(genCommID(info));
          args.push_back(info->lineno);
          args.push_back(gFilenameLookupCache[info->filename]);

          codegenCallWithArgs(fn.c_str(), args);
        }
      }
    }
  }
}


/************************************* | **************************************
*                                                                             *
*                                                                             *
************************************** | *************************************/

//
// Codegen assignments like a += b, a -= b, a *= b, etc.
//   op is the C string for the operator (e.g., " += ")
//   codegenOp is a function pointer to the codegenAdd()-style
//     routine that would be used to generate the operator
//     itself in a normal context (used by LLVM)
//
static
void codegenOpAssign(GenRet a, GenRet b, const char* op,
                     GenRet (*codegenOp)(GenRet a, GenRet b))
{
  GenInfo* info = gGenInfo;

  // deref 'a' if it is a 'ref' argument
  GenRet ap;
  if (a.chplType->symbol->hasFlag(FLAG_REF) ||
      a.chplType->symbol->hasFlag(FLAG_WIDE_REF) ||
      a.chplType->symbol->hasFlag(FLAG_WIDE_CLASS)) {
    ap = codegenDeref(a);
  } else {
    ap = a;
  }
  GenRet bv = codegenValue(b);  // get the value of 'b'

  bool aIsRemote = ap.isLVPtr == GEN_WIDE_PTR;
  GenRet aLocal;              // a guaranteed-local copy of a

  // For a wide pointer, we copy in and copy out...
  if( aIsRemote ) {
    // copy in -- will result in a chpl_comm_get(...)
    aLocal = createTempVar(ap.chplType);
    codegenAssign(aLocal, ap);
  } else {
    // otherwise, it's already local
    aLocal = ap;
  }

  if( info->cfile ) {
    // generate the local C statement
    std::string stmt = codegenValue(aLocal).c + op + bv.c + ";\n";
    info->cStatements.push_back(stmt);
  } else {
    // LLVM version of a += b is just a = a + b.
    codegenAssign(aLocal, codegenOp(codegenValue(aLocal), bv));
  }

  if( aIsRemote ) {
    // copy out -- will result in a chpl_comm_put(...)
    codegenAssign(ap, aLocal);
  }
}

static GenRet codegen_prim_get_real(GenRet arg, Type* type, bool real) {
  GenRet ret;
  const char* realOrImag;
  char fnName[21];
  if (real) {
    realOrImag = "Real";
  } else {
    realOrImag = "Imag";
  }
  if (type == dtComplex[COMPLEX_SIZE_64]->refType) {
    snprintf(fnName, 21, "complex64Get%sRef", realOrImag);
    ret = codegenCallExpr(fnName, arg);
  } else if (type == dtComplex[COMPLEX_SIZE_128]->refType) {
    snprintf(fnName, 21, "complex128Get%sRef", realOrImag);
    ret = codegenCallExpr(fnName, arg);
  } else if (type == dtComplex[COMPLEX_SIZE_64]) {
    snprintf(fnName, 21, "complex64Get%sRef", realOrImag);
    ret = codegenCallExpr(fnName, codegenAddrOf(arg));
  } else if (type == dtComplex[COMPLEX_SIZE_128]) {
    snprintf(fnName, 21, "complex128Get%sRef", realOrImag);
    ret = codegenCallExpr(fnName, codegenAddrOf(arg));
  } else {
    INT_FATAL("Unsupported type in PRIM_GET_REAL");
  }
  return ret;
}

static void codegenEmbedChapelAstMetadata(GenRet& gr, BaseAST* ast) {
  #ifdef HAVE_LLVM
  if (gGenInfo->cfile == nullptr && gr.val && fGenIDS) {
    if (auto insn = llvm::dyn_cast<llvm::Instruction>(gr.val)) {
      llvm::LLVMContext& ctx = gContext->llvmContext();
      llvm::Type *int64Ty = llvm::Type::getInt64Ty(ctx);
      llvm::Constant* c = llvm::ConstantInt::get(int64Ty, ast->id);
      llvm::ConstantAsMetadata* aid = llvm::ConstantAsMetadata::get(c);
      llvm::MDNode* node = llvm::MDNode::get(ctx, aid);
      insn->setMetadata("chpl.ast.id", node);
    }
  }
  #else
  std::ignore = gr;
  std::ignore = ast;
  #endif
}

GenRet codegenProcedurePointerFetch(Expr* baseExpr) {
  auto ft1 = toFunctionType(baseExpr->typeInfo()->getValType());
  INT_ASSERT(ft1);

  GenRet ret = codegenValueMaybeDeref(baseExpr);
  if (ft1->isWide()) {
    std::vector<GenRet> args(1);
    args[0] = ret;
    auto fname = "chpl_dynamicProcIdxToLocalPtr";
    ret = codegenCallExprWithArgs(fname, args);
    ret.chplType = ft1->getAsLocal();
  }

  // If we had a wide function, we should have a local pointer now.
  auto ft2 = toFunctionType(ret.chplType);
  INT_ASSERT(ft2 && ft2->isLocal());
  INT_ASSERT(ret.isLVPtr == GEN_VAL);

  return ret;
}

static GenRet codegenCall(CallExpr* call) {
  GenRet ret;

  FnSymbol* fn = call->resolvedFunction();
  FunctionType* chplFnType = nullptr;
  std::vector<GenRet> args(call->numActuals());
  GenRet base;

  if (call->isIndirectCall()) {
    base = codegenProcedurePointerFetch(call->baseExpr);

    // Use the type from the generated expression since it should be local.
    chplFnType = toFunctionType(base.chplType);

    // And make sure the LLVM type for the function is generated.
    if (chplFnType) chplFnType->codegenDef();

    INT_ASSERT(chplFnType);
    INT_ASSERT(chplFnType->isLocal());
    INT_ASSERT(call->numActuals() == chplFnType->numFormals());
  } else if (fn) {
    auto se = toSymExpr(call->baseExpr);
    INT_ASSERT(se && se->symbol() == fn);
    base = fn->codegenAsCallBaseExpr();
  } else {
    // It's not a base expression we should be handling...
    INT_FATAL(call, "Should not reach here!");
  }

  bool isCallToExtern = (chplFnType && chplFnType->isExtern()) ||
                        (fn && fn->hasFlag(FLAG_EXTERN));

  for (int i = 0; i < call->numActuals(); i++) {
    auto formal = fn ? fn->getFormal(i+1) : nullptr;
    auto actual = call->get(i+1);
    bool needDerefForActual = false;

    if (formal) {
      needDerefForActual = !formal->isRefOrWideRef();
    } else {
      INT_ASSERT(chplFnType);
      auto ftFormal = chplFnType->formal(i);
      needDerefForActual = !ftFormal->qualType().isRefOrWideRef();
    }

    SymExpr* se = toSymExpr(actual);
    GenRet arg = actual;

    if (se && isFnSymbol(se->symbol())) {
      if (isCallToExtern || (formal && formal->type == dtCFnPtr)) {
        arg = codegenCast("c_fn_ptr", arg);
      }
    }

    // If actual is special primitive arg will be modified
    // Otherwise, arg is untouched
    std::ignore = codegenIsSpecialPrimitive(formal, actual, arg);

    // Handle passing strings to externs
    //should this be else if?
    if (isCallToExtern) {
      if (actual->isWideRef() || arg.isLVPtr == GEN_WIDE_PTR) {
        arg = codegenValue(arg);

      } else if (formal && call->isRefExternStarTuple(formal, actual)) {
        // In C, a fixed-size-array lvalue is already a pointer,
        // so we deref here. But for LLVM, if we deref we will
        // end up passing the tuple by value, which is not right.
        if (gGenInfo->cfile != NULL) arg = codegenDeref(arg);
      }
    }

    // TODO: What if the actual is a wide-ref and the formal is a narrow ref?
    // Should that be handled back in IWR?
    if (arg.chplType->symbol->isRefOrWideRef() && needDerefForActual) {
      arg = codegenValue(codegenDeref(arg));
    }

    args[i] = arg;
  }

  // LLVM
  #ifdef HAVE_LLVM
    if (gGenInfo->cfile == nullptr) {
      // handle any special cases for which
      // bool isBuiltinExternCFunction(const char* cname) returns true.
      //
      // special case: for CallExpr sizeof(..)
      if (isCallToExtern && fn && !strcmp(fn->name, "sizeof")) {
        if (args[0].type) {
          return codegenSizeof(args[0].type);
        } else {
          return codegenSizeof(codegenValue(args[0]).val->getType());
        }
      }
    }
  #endif

  // Generate the body of the call.
  auto cname = fn ? fn->cname : nullptr;
  ret = codegenCallExprWithArgs(base, args, cname, fn, nullptr, true);

  // C: Append a semicolon for end of statement if needed.
  if (gGenInfo->cfile != nullptr) {
    if (call->getStmtExpr() == call) {
      gGenInfo->cStatements.push_back(ret.c + ";\n");
    }
  }

  return ret;
}

/* Notes about code generation:
 *  Intermediate expressions are returned from Expr::codegen
 *  Local variables, array elements, tuple elements, and fields
 *    are always generated as the address of that value (ie lvalues
 *    are pointers)
 *  Expressions may be actual values, not addresses
 *  Chapel includes class instances - which may be remote or local.
 *   Note that the variable in question for a class instance *is*
 *    a wide (or not) reference (to the allocated object), but these
 *    references are considered "values" rather than "lvalue pointers"
 *    by the code generator. Thus a "reference to AGGREGATE_CLASS" is
 *    actually a reference to a reference.. Note also that an "ARRAY"
 *    in the code generator is actually an instance of the e.g. _ddata
 *    class (and so the pointer to the data is again treated as a value).
 *    Lastly, Chapel references are considered "values" rather
 *    than "lvalue pointers", similarly to class instances, so that
 *    the generated code can set what a reference is pointing to
 *    (even though that is not allowed in Chapel).
 */
GenRet CallExpr::codegen() {
  SET_LINENO(this);

  FnSymbol* fn = resolvedFunction();
  GenRet ret;

  // Note (for debugging), function name is in parentSymbol->cname.
  if (id == breakOnCodegenID) debuggerBreakHere();

  if (getStmtExpr() == this) codegenStmt(this);

  INT_ASSERT(fn || primitive != nullptr || this->isIndirectCall());
  bool canGenerate = (fn && !fn->hasFlag(FLAG_NO_CODEGEN)) ||
                     (this->isIndirectCall());

  if (primitive != nullptr) {
    ret = codegenPrimitive();

  } else if (fn && fn->hasFlag(FLAG_ON_BLOCK)) {
    codegenInvokeOnFun();

  } else if (fn && fn->hasFlag(FLAG_BEGIN_BLOCK)) {
    codegenInvokeTaskFun("chpl_taskAddBegin");

  } else if (fn && fn->hasFlag(FLAG_COBEGIN_OR_COFORALL_BLOCK)) {
    codegenInvokeTaskFun("chpl_taskAddCoStmt");

  } else if (canGenerate) {
    ret = codegenCall(this);
  }

  // When generating LLVM value, if --gen-ids is on,
  // add metadata nodes that have the Chapel AST ids
  codegenEmbedChapelAstMetadata(ret, this);

  return ret;
}

// to define a primitive's code generation method
#define DEFINE_PRIM(NAME) \
  void CallExpr::codegen ## NAME (CallExpr* call, GenRet &ret)

// ditto but where it just calls codegenBasicPrimitiveExpr
#define DEFINE_BASIC_PRIM(NAME) \
  void CallExpr::codegen ## NAME (CallExpr* call, GenRet &ret) { \
    ret = call->codegenBasicPrimitiveExpr(); \
  }

// to call a primitive's code generation method
#define CODEGEN_PRIM(NAME, call, ret) \
  codegen ## NAME (call, ret);

// to call another primitive's DEFINE_PRIM
#define FORWARD_PRIM(NAME) \
  CODEGEN_PRIM(NAME, call, ret);

DEFINE_PRIM(UNKNOWN) {
    // This is handled separately
    INT_ASSERT(false);
}

DEFINE_PRIM(ARRAY_SET) {
    // get(1): (wide?) base pointer
    // get(2): index
    // get(3): value

    // Used to handle FLAG_WIDE_CLASS/FLAG_STAR_TUPLE specially,
    // but these should be taken care of by codegenElementPtr and
    // codegenAssign now.
    GenRet elementPtr = codegenElementPtr(call->get(1), call->get(2));
    codegenAssign(elementPtr, call->get(3));
}
DEFINE_PRIM(ARRAY_SET_FIRST) {
    FORWARD_PRIM(ARRAY_SET);
}
DEFINE_PRIM(ARRAY_SHIFT_BASE_POINTER) {
    // get(1): local return value
    // get(2): _ddata instance
    // get(3): integral amount to shift by
    GenRet rv      = call->get(1);
    GenRet addr    = call->get(2);
    GenRet shifted = codegenElementPtr(addr, call->get(3), true);

    // used to have code to handle this case but it was wrong
    INT_ASSERT(rv.isLVPtr != GEN_WIDE_PTR);

    // not useful to get a reference type in this case
    GenRet shiftedAddr = codegenAddrOf(shifted);
    if (shiftedAddr.chplType) {
      shiftedAddr.chplType = shiftedAddr.chplType->getValType();
    }
    codegenAssign(rv, shiftedAddr);
}

DEFINE_PRIM(NOOP) {
}
DEFINE_PRIM(MOVE) {
    INT_FATAL("Handled in switch");
}

DEFINE_PRIM(DEREF) { codegenIsSpecialPrimitive(NULL, call, ret); }
DEFINE_PRIM(GET_SVEC_MEMBER_VALUE) { codegenIsSpecialPrimitive(NULL, call, ret); }
DEFINE_PRIM(GET_MEMBER_VALUE) { codegenIsSpecialPrimitive(NULL, call, ret); }
DEFINE_PRIM(ARRAY_GET) { codegenIsSpecialPrimitive(NULL, call, ret); }
DEFINE_PRIM(ON_LOCALE_NUM) { codegenIsSpecialPrimitive(NULL, call, ret); }
DEFINE_PRIM(GET_REAL) { codegenIsSpecialPrimitive(NULL, call, ret); }
DEFINE_PRIM(GET_IMAG) { codegenIsSpecialPrimitive(NULL, call, ret); }

DEFINE_PRIM(WIDE_MAKE) {
    // (type, localeID, addr)
    Type* narrowType = call->get(1)->typeInfo();
    if (narrowType->symbol->hasFlag(FLAG_WIDE_CLASS)) {
      narrowType = narrowType->getField("addr")->typeInfo();
    }
    INT_ASSERT(!narrowType->symbol->hasFlag(FLAG_WIDE_CLASS));
    GenRet locale = call->get(2)->codegen();
    GenRet raddr = codegenValue(call->get(3)->codegen());

    if (raddr.chplType && raddr.chplType->isRef() && !narrowType->isRef()) {
      raddr = codegenValue(codegenDeref(raddr));
    }

    ret = codegenCast(narrowType, raddr, true);

    if (requireWideReferences()) {
      ret = codegenWideAddr(locale, ret);
    }
}
DEFINE_PRIM(WIDE_GET_LOCALE) {
    if (call->get(1)->isWideRef() ||
        call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
      ret = codegenRlocale(call->get(1));
    } else {
      ret = codegenGetLocaleID();
    }
}
DEFINE_PRIM(WIDE_GET_NODE) {
    if (call->get(1)->isWideRef() ||
        call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
      ret = codegenRnode(call->get(1));
    } else {
      ret = codegenGetNodeID();
    }
}

DEFINE_PRIM(WIDE_GET_ADDR) {
    if (call->get(1)->isWideRef() ||
        call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
      ret = codegenRaddr(call->get(1));
    } else {
      ret = codegenValue(call->get(1));
    }

    // _wide_get_addr promises to return a c void ptr.  Hence the cast.
    ret = codegenCast(dtCVoidPtr, ret);
    ret.isUnsigned = true;
}

DEFINE_PRIM(SET_REFERENCE) {
    // Special handling for reference variables
    // These variables have value type so PRIM_ADDR_OF
    // should just return the reference.
    if (call->get(1)->isRefOrWideRef()) {
      ret = codegenValue(call->get(1));
    } else {
      ret = codegenAddrOf(call->get(1));
    }
}

DEFINE_PRIM(ADDR_OF) {
    FORWARD_PRIM(SET_REFERENCE);
}

DEFINE_PRIM(REF_TO_STRING) {
    if (call->get(1)->isWideRef() ||
        call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
      GenRet wide = call->get(1);

      ret = codegenCallExpr("chpl_wideRefToString",
                            codegenRnode(wide),
                            codegenRaddr(wide));
    } else {
      ret = codegenCallExpr("chpl_refToString", call->get(1));
    }
}

DEFINE_PRIM(CLASS_NAME_BY_ID) {
    GenRet cid = codegenValue(call->get(1));
    const char* eltType = dtStringC->symbol->cname;
    ret = codegenGlobalArrayElement("chpl_classNames", eltType, cid);
}

DEFINE_PRIM(RETURN) {
  bool returnVoid = call->typeInfo() == dtVoid || call->typeInfo() == dtNothing;
  if (!returnVoid) {
    GenRet retExpr = call->get(1);
    if (!call->typeInfo()->symbol->isRefOrWideRef() && call->get(1)->isRefOrWideRef()) {
      retExpr = codegenDeref(retExpr);
    }
    ret = codegenValue(retExpr);
  }

  if (gGenInfo->cfile) {
    if (returnVoid) {
      ret.c = "return";
    } else {
      ret.c = "return " + ret.c;
    }
  } else {
#ifdef HAVE_LLVM
    GenInfo* info = gGenInfo;
    llvm::IRBuilder<>* irBuilder = info->irBuilder;
    llvm::Instruction* returnInst = NULL;
    llvm::Function* curFn = irBuilder->GetInsertBlock()->getParent();
    llvm::Type* returnType = ret.type;

    const clang::CodeGen::ABIArgInfo* returnInfo = NULL;
    if (gGenInfo->currentFunctionABI)
      returnInfo = &info->currentFunctionABI->getReturnInfo();

    if (call->parentSymbol->hasFlag(FLAG_FUNCTION_TERMINATES_PROGRAM) &&
        !gCodegenGPU /* can't immediately halt on GPU */) {
      returnInst = irBuilder->CreateUnreachable();
      trackLLVMValue(returnInst);
    } else if (returnInfo) {
      // See CodeGenFunction::EmitFunctionEpilog

      switch (returnInfo->getKind()) {
        case clang::CodeGen::ABIArgInfo::Kind::Ignore:
        {
          returnInst = irBuilder->CreateRetVoid();
          trackLLVMValue(returnInst);
          break;
        }

        case clang::CodeGen::ABIArgInfo::Kind::InAlloca:
        {
          // TODO: where to store ret.val ?
          INT_FATAL("TODO");

          if (returnInfo->getInAllocaSRet()) {
            // handle returning the sret value in a register
            llvm::Function::arg_iterator ii = curFn->arg_end();
            --ii;
            llvm::Value* arg = &*ii;
            llvm::Value* sret = irBuilder->CreateStructGEP(
                nullptr, arg, returnInfo->getInAllocaFieldIndex());
            trackLLVMValue(sret);

            llvm::MaybeAlign align = getPointerAlign();
            llvm::Value* v = irBuilder->CreateAlignedLoad(sret->getType(),
                                                          sret,
                                                          align,
                                                          "sret");
            trackLLVMValue(v);

            returnInst = irBuilder->CreateRet(v);
            trackLLVMValue(returnInst);
          } else {
            returnInst = irBuilder->CreateRetVoid();
            trackLLVMValue(returnInst);
          }
          break;
        }
        case clang::CodeGen::ABIArgInfo::Kind::IndirectAliased:
        {
          INT_FATAL("IndirectAliased not implemented yet");
          break;
        }
        case clang::CodeGen::ABIArgInfo::Kind::Indirect:
        {
          auto ii = curFn->arg_begin();
          if (returnInfo->isSRetAfterThis())
            ++ii;

          llvm::Value* arg = &*ii;
          GenRet ptr;
          ptr.val = arg;
          ptr.isLVPtr = GEN_PTR;
          ptr.chplType = call->typeInfo();

          codegenStoreLLVM(ret, ptr);
          returnInst = irBuilder->CreateRetVoid();
          trackLLVMValue(returnInst);
          break;
        }

        case clang::CodeGen::ABIArgInfo::Kind::Extend:
        case clang::CodeGen::ABIArgInfo::Kind::Direct:
        {
          if (returnInfo->getCoerceToType() == returnType &&
              returnInfo->getDirectOffset() == 0) {
            returnInst = irBuilder->CreateRet(ret.val);
            trackLLVMValue(returnInst);
          } else {
            // offset might be nonzero... what does that mean?
            if (returnInfo->getDirectOffset() != 0)
              INT_FATAL("Not implemented yet");

            llvm::Value* r = convertValueToType(ret.val,
                                                returnInfo->getCoerceToType(),
                                                !ret.isUnsigned,
                                                /*force*/ true);
            returnInst = irBuilder->CreateRet(r);
            trackLLVMValue(returnInst);
          }
          break;
        }

        case clang::CodeGen::ABIArgInfo::Kind::CoerceAndExpand:
        {
          llvm::StructType* toTy = returnInfo->getCoerceAndExpandType();
          llvm::Value* r = convertValueToType(ret.val,
                                              toTy,
                                              !ret.isUnsigned,
                                              /*force*/ true);

          // gather the coerced elements that aren't padding
          llvm::SmallVector<llvm::Value*, 4> results;
          unsigned nElts = toTy->getNumElements();
          for (unsigned i = 0; i < nElts; i++ ) {
            auto eltTy = toTy->getElementType(i);
            if (clang::CodeGen::ABIArgInfo::isPaddingForCoerceAndExpand(eltTy))
              continue;

            auto elt = irBuilder->CreateExtractValue(r, i);
            trackLLVMValue(elt);
            results.push_back(elt);
          }

          // single result should be returned
          if (results.size() == 1) {
            returnInst = irBuilder->CreateRet(results[0]);
            trackLLVMValue(returnInst);

          // Otherwise, form new aggregate without padding
          } else {
            llvm::Type *returnTy = returnInfo->getUnpaddedCoerceAndExpandType();

            llvm::Value* rv = llvm::UndefValue::get(returnTy);
            unsigned nResults = results.size();
            for (unsigned i = 0; i < nResults; i++) {
              rv = irBuilder->CreateInsertValue(rv, results[i], i);
              trackLLVMValue(rv);
            }
            returnInst = irBuilder->CreateRet(rv);
            trackLLVMValue(returnInst);
          }

          break;
        }

        case clang::CodeGen::ABIArgInfo::Kind::Expand:
          INT_FATAL("not implemented yet");
          break;

        // No default -> compiler warning if more added
      }

    } else {
      if (returnVoid) {
        returnInst = irBuilder->CreateRetVoid();
        trackLLVMValue(returnInst);
      } else {
        ret = codegenCast(ret.chplType, ret);
        returnInst = irBuilder->CreateRet(ret.val);
        trackLLVMValue(returnInst);
      }
    }
    ret.val = returnInst;

    if (returnInst) {
      // Since putting anything after the return statement will make the LLVM
      // IR invalid, we set the insert point before the return instruction so that
      // other compilation processes work fine and as expected.
      gGenInfo->irBuilder->SetInsertPoint(returnInst);
    }
#endif
  }
}
DEFINE_PRIM(UNARY_MINUS) {
  ret = codegenNeg(call->get(1));
}
DEFINE_PRIM(UNARY_PLUS) {
    GenRet tmp = codegenValue(call->get(1));

    if (gGenInfo->cfile)
      ret.c = "(+ " + tmp.c + ")";
    else
      ret = tmp; // nothing is necessary.
  }
DEFINE_PRIM(UNARY_NOT) {
    GenRet tmp = codegenValue(call->get(1));

    if (gGenInfo->cfile) {
      ret.c = "(~ " + tmp.c + ")";
    } else {
#ifdef HAVE_LLVM
      ret.val = gGenInfo->irBuilder->CreateNot(tmp.val);
      trackLLVMValue(ret.val);
#endif
    }
}
DEFINE_PRIM(UNARY_LNOT) {
  ret = codegenIsZero(call->get(1));
}
DEFINE_PRIM(SQRT) {
  ret = codegenSqrt(call->get(1));
}
DEFINE_PRIM(ABS) {
  ret = codegenAbs(call->get(1));
}
DEFINE_PRIM(ADD) {
    ret = codegenAdd(call->get(1), call->get(2));
}
DEFINE_PRIM(SUBTRACT) {
    ret = codegenSub(call->get(1), call->get(2));
}
DEFINE_PRIM(MULT) {
    ret = codegenMul(call->get(1), call->get(2));
}
DEFINE_PRIM(DIV) {
    ret = codegenDiv(call->get(1), call->get(2));
}
DEFINE_PRIM(MOD) {
    ret = codegenMod(call->get(1), call->get(2));
}
DEFINE_PRIM(FMA) {
    ret = codegenFma(call->get(1), call->get(2), call->get(3));
}
DEFINE_PRIM(LSH) {
    ret = codegenLsh(call->get(1), call->get(2));
}
DEFINE_PRIM(RSH) {
    ret = codegenRsh(call->get(1), call->get(2));
}
DEFINE_PRIM(PTR_EQUAL) {
    FORWARD_PRIM(EQUAL);
}
DEFINE_PRIM(EQUAL) {
    if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
        call->get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
      // TODO: The locale model will eventually have to provide a function
      // to compare wide addresses.  Ditto for the NOTEQUAL case below.
      GenRet a      = call->get(1);
      GenRet b      = call->get(2);
      GenRet addrNe = codegenNotEquals(codegenRaddr(a), codegenRaddr(b));
      GenRet locNe  = codegenNotEquals(codegenRnode(a), codegenRnode(b));
      GenRet rh     = codegenLogicalAnd(codegenIsNotZero(codegenRaddr(a)),
                                        locNe);
      GenRet ne     = codegenLogicalOr(addrNe, rh);

      ret = codegenIsZero(ne);

    } else if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
               call->get(2)->typeInfo() == dtNil) {
      ret = codegenIsZero(call->get(1));

    } else if (call->get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
               call->get(1)->typeInfo() == dtNil) {
      ret = codegenIsZero(call->get(2));

    } else {
      ret = codegenEquals(call->get(1), call->get(2));
    }
}
DEFINE_PRIM(PTR_NOTEQUAL) {
    FORWARD_PRIM(NOTEQUAL);
}
DEFINE_PRIM(NOTEQUAL) {
    if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
        call->get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
      // TODO: The locale model will eventually have to provide a function
      // to compare wide addresses.  Ditto for the EQUAL case above.
      GenRet a      = call->get(1);
      GenRet b      = call->get(2);
      GenRet addrNe = codegenNotEquals(codegenRaddr(a), codegenRaddr(b));
      GenRet locNe  = codegenNotEquals(codegenRnode(a), codegenRnode(b));
      GenRet rh     = codegenLogicalAnd(codegenIsNotZero(codegenRaddr(a)),
                                        locNe);

      ret = codegenLogicalOr(addrNe, rh);

    } else if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
               call->get(2)->typeInfo() == dtNil) {
      ret = codegenIsNotZero(call->get(1));

    } else if (call->get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
               call->get(1)->typeInfo() == dtNil) {
      ret = codegenIsNotZero(call->get(2));

    } else {
      ret = codegenNotEquals(call->get(1), call->get(2));
    }
}
DEFINE_PRIM(LESSOREQUAL) {
    GenRet a = call->get(1);
    GenRet b = call->get(2);

    if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
    if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);

    GenRet av = codegenValue(a);
    GenRet bv = codegenValue(b);

    if (gGenInfo->cfile) {
      ret.c = "(" + av.c + " <= " + bv.c + ")";
    } else {
#ifdef HAVE_LLVM
      PromotedPair values = convertValuesToLarger(
                                   av.val,
                                   bv.val,
                                   is_signed(call->get(1)->typeInfo()),
                                   is_signed(call->get(2)->typeInfo()));

      if (values.a->getType()->isFPOrFPVectorTy()) {
        ret.val = gGenInfo->irBuilder->CreateFCmpOLE(values.a, values.b);
        trackLLVMValue(ret.val);

      } else if (!values.isSigned) {
        ret.val = gGenInfo->irBuilder->CreateICmpULE(values.a, values.b);
        trackLLVMValue(ret.val);

      } else {
        ret.val = gGenInfo->irBuilder->CreateICmpSLE(values.a, values.b);
        trackLLVMValue(ret.val);
      }
#endif
    }
}
DEFINE_PRIM(GREATEROREQUAL) {
    GenRet a = call->get(1);
    GenRet b = call->get(2);

    if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
    if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);

    GenRet av = codegenValue(a);
    GenRet bv = codegenValue(b);

    if (gGenInfo->cfile) {
      ret.c = "(" + av.c + " >= " + bv.c + ")";
    } else {
#ifdef HAVE_LLVM
      PromotedPair values = convertValuesToLarger(
                                   av.val,
                                   bv.val,
                                   is_signed(call->get(1)->typeInfo()),
                                   is_signed(call->get(2)->typeInfo()));

      if (values.a->getType()->isFPOrFPVectorTy()) {
        ret.val = gGenInfo->irBuilder->CreateFCmpOGE(values.a, values.b);
        trackLLVMValue(ret.val);

      } else if (!values.isSigned) {
        ret.val = gGenInfo->irBuilder->CreateICmpUGE(values.a, values.b);
        trackLLVMValue(ret.val);

      } else {
        ret.val = gGenInfo->irBuilder->CreateICmpSGE(values.a, values.b);
        trackLLVMValue(ret.val);
      }
#endif
    }
}
DEFINE_PRIM(LESS) {
    GenRet a = call->get(1);
    GenRet b = call->get(2);

    if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
    if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);

    GenRet av = codegenValue(a);
    GenRet bv = codegenValue(b);

    if (gGenInfo->cfile) {
      ret.c = "(" + av.c + " < " + bv.c + ")";
    } else {
#ifdef HAVE_LLVM
      PromotedPair values = convertValuesToLarger(
                                   av.val,
                                   bv.val,
                                   is_signed(call->get(1)->typeInfo()),
                                   is_signed(call->get(2)->typeInfo()));

      if (values.a->getType()->isFPOrFPVectorTy()) {
        ret.val = gGenInfo->irBuilder->CreateFCmpOLT(values.a, values.b);
        trackLLVMValue(ret.val);

      } else if (!values.isSigned) {
        ret.val = gGenInfo->irBuilder->CreateICmpULT(values.a, values.b);
        trackLLVMValue(ret.val);

      } else {
        ret.val = gGenInfo->irBuilder->CreateICmpSLT(values.a, values.b);
        trackLLVMValue(ret.val);
      }
#endif
    }
}
DEFINE_PRIM(GREATER) {
    GenRet a = call->get(1);
    GenRet b = call->get(2);

    if (a.chplType && a.chplType->symbol->isRefOrWideRef()) a = codegenDeref(a);
    if (b.chplType && b.chplType->symbol->isRefOrWideRef()) b = codegenDeref(b);

    GenRet av = codegenValue(a);
    GenRet bv = codegenValue(b);

    if (gGenInfo->cfile) {
      ret.c = "(" + av.c + " > " + bv.c + ")";
    } else {
#ifdef HAVE_LLVM
      PromotedPair values = convertValuesToLarger(
                                   av.val,
                                   bv.val,
                                   is_signed(call->get(1)->typeInfo()),
                                   is_signed(call->get(2)->typeInfo()));

      if (values.a->getType()->isFPOrFPVectorTy()) {
        ret.val = gGenInfo->irBuilder->CreateFCmpOGT(values.a, values.b);
        trackLLVMValue(ret.val);

      } else if (!values.isSigned) {
        ret.val = gGenInfo->irBuilder->CreateICmpUGT(values.a, values.b);
        trackLLVMValue(ret.val);

      } else {
        ret.val = gGenInfo->irBuilder->CreateICmpSGT(values.a, values.b);
        trackLLVMValue(ret.val);
      }
#endif
    }
}

DEFINE_PRIM(AND) {
    ret = codegenAnd(call->get(1), call->get(2));
}
DEFINE_PRIM(OR) {
    ret = codegenOr(call->get(1), call->get(2));
}
DEFINE_PRIM(XOR) {
    ret = codegenXor(call->get(1), call->get(2));
}

DEFINE_PRIM(ASSIGN) {
  Expr*       lhs        = call->get(1);
  Expr*       rhs        = call->get(2);
  TypeSymbol* lhsTypeSym = lhs->typeInfo()->symbol;
  TypeSymbol* rhsTypeSym = rhs->typeInfo()->symbol;

  // PRIM_ASSIGN differs from PRIM_MOVE in that PRIM_ASSIGN always copies
  // objects.  PRIM_MOVE can be used to copy a pointer (i.e. reference)
  // into another pointer, but if you try this with PRIM_ASSIGN, instead
  // it will overwrite what the LHS points to with what the RHS points to.

  // TODO:  Works but may be slow.
  // (See the implementation of PRIM_MOVE above for several peephole
  // optimizations depending on specifics of the RHS expression.)

  // PRIM_ASSIGN expects either a narrow or wide pointer as its LHS arg.
  GenRet lg;
  GenRet rg;

  if (lhs->isRefOrWideRef()) {
    lg = codegenDeref(lhs);
    lhsTypeSym = lhsTypeSym->getValType()->symbol;
  } else {
    lg = lhs->codegen();
  }

  if (rhs->isRefOrWideRef()) {
    rg = codegenDeref(rhs);
    rhsTypeSym = rhsTypeSym->getValType()->symbol;
  } else {
    rg = rhs->codegen();
  }

  if (lhsTypeSym->hasFlag(FLAG_WIDE_CLASS) == false &&
      rhsTypeSym->hasFlag(FLAG_WIDE_CLASS) == true)
    rg = codegenRaddr(rg);

  if (lhsTypeSym->hasFlag(FLAG_WIDE_CLASS) == true &&
      rhsTypeSym->hasFlag(FLAG_WIDE_CLASS) == false)
    rg = codegenWideHere(rg);

  if (!lg.chplType)
    lg.chplType = lhsTypeSym->type;
  if (!rg.chplType)
    rg.chplType = rhsTypeSym->type;

  codegenAssign(lg, rg);
}

DEFINE_PRIM(UNORDERED_ASSIGN) {

  Expr* lhsExpr = call->get(1);
  Expr* rhsExpr = call->get(2);
  bool lhsWide = lhsExpr->isWideRef();
  bool rhsWide = rhsExpr->isWideRef();

  // Both sides are narrow, do a normal assign
  if (!lhsWide && !rhsWide) {
    FORWARD_PRIM(ASSIGN);
    return;
  }

  GenRet dst = lhsExpr;
  bool dstRef = lhsExpr->typeInfo()->symbol->hasFlag(FLAG_REF);
  GenRet src = rhsExpr;
  bool srcRef = rhsExpr->typeInfo()->symbol->hasFlag(FLAG_REF);
  GenRet ln = call->get(3);
  GenRet fn = call->get(4);
  TypeSymbol* dt = lhsExpr->typeInfo()->getValType()->symbol;
  GenRet size = codegenSizeof(dt->typeInfo());

  if (!lhsWide && rhsWide) {
    // do an unordered GET
    // chpl_gen_comm_get_unordered(void *dst,
    //   c_nodeid_t src_locale, void* src_raddr,
    //   size_t size, int32_t commID,
    //   int ln, int32_t fn);

    dst = codegenValuePtr(dst);
    if (dstRef)
      dst = codegenDeref(dst);

    codegenCall("chpl_gen_comm_get_unordered",
                codegenCastToVoidStar(codegenAddrOf(dst)),
                codegenRnode(src),
                codegenRaddr(src),
                size,
                genCommID(gGenInfo),
                ln, fn);
  } else if (lhsWide && !rhsWide) {
    // do an unordered PUT
    // chpl_gen_comm_put_unordered(void *src,
    //   c_nodeid_t dst_locale, void* dst_raddr,
    //   size_t size, int32_t commID,
    //   int ln, int32_t fn);

    src = codegenValuePtr(src);
    if (srcRef)
      src = codegenDeref(src);

    codegenCall("chpl_gen_comm_put_unordered",
                codegenCastToVoidStar(codegenAddrOf(src)),
                codegenRnode(dst),
                codegenRaddr(dst),
                size,
                genCommID(gGenInfo),
                ln, fn);
  } else {
    // do an unordered GETPUT
    // chpl_comm_getput_unordered(
    //   c_nodeid_t dst_locale, void* dst_raddr,
    //   c_nodeid_t src_locale, void* src_raddr,
    //   size_t size, int32_t commID,
    //   int ln, int32_t fn);
    codegenCall("chpl_gen_comm_getput_unordered",
                codegenRnode(dst),
                codegenRaddr(dst),
                codegenRnode(src),
                codegenRaddr(src),
                size,
                genCommID(gGenInfo),
                ln, fn);
  }
}
DEFINE_PRIM(ADD_ASSIGN) {
    codegenOpAssign(call->get(1), call->get(2), " += ", codegenAdd);
}
DEFINE_PRIM(SUBTRACT_ASSIGN) {
    codegenOpAssign(call->get(1), call->get(2), " -= ", codegenSub);
}
DEFINE_PRIM(MULT_ASSIGN) {
    codegenOpAssign(call->get(1), call->get(2), " *= ", codegenMul);
}
DEFINE_PRIM(DIV_ASSIGN) {
    codegenOpAssign(call->get(1), call->get(2), " /= ", codegenDiv);
}
DEFINE_PRIM(MOD_ASSIGN) {
    codegenOpAssign(call->get(1), call->get(2), " %= ", codegenMod);
}
DEFINE_PRIM(LSH_ASSIGN) {
    codegenOpAssign(call->get(1), call->get(2), " <<= ", codegenLsh);
}
DEFINE_PRIM(RSH_ASSIGN) {
    codegenOpAssign(call->get(1), call->get(2), " >>= ", codegenRsh);
}
DEFINE_PRIM(AND_ASSIGN) {
    codegenOpAssign(call->get(1), call->get(2), " &= ", codegenAnd);
}
DEFINE_PRIM(OR_ASSIGN) {
    codegenOpAssign(call->get(1), call->get(2), " |= ", codegenOr);
}
DEFINE_PRIM(XOR_ASSIGN) {
    codegenOpAssign(call->get(1), call->get(2), " ^= ", codegenXor);
}
DEFINE_PRIM(LOGICALAND_ASSIGN) {
    codegenOpAssign(call->get(1), call->get(2), " &&= ", codegenLogicalAnd);
}
DEFINE_PRIM(LOGICALOR_ASSIGN) {
    codegenOpAssign(call->get(1), call->get(2), " ||= ", codegenLogicalOr);
}
DEFINE_PRIM(POW) {
    ret = codegenCallExpr("pow", call->get(1), call->get(2));
}

DEFINE_PRIM(MIN) {
    Type* t = call->get(1)->typeInfo();

    if (is_arithmetic_type( t)) {
      if (is_int_type( t)) {
        ret = codegenUseGlobal("MIN_INT" + numToString(get_width(t)));

      } else if (is_uint_type( t)) {
        ret = codegenUseGlobal("MIN_UINT" + numToString(get_width(t)));

      } else if (is_real_type( t)) {
        std::string width = numToString(get_width(t));

        ret = codegenNeg(codegenUseGlobal("MAX_FLOAT" + width));

      } else if (is_imag_type( t)) {
        std::string width = numToString(get_width(t));

        ret = codegenNeg(codegenUseGlobal("MAX_FLOAT" + width));

      } else if (is_complex_type( t)) {
        std::string width     = numToString(get_width(t));
        std::string halfWidth = numToString(get_width(t) / 2);

        std::string fname     = "_chpl_complex" + width;
        std::string maxFloat  = "MAX_FLOAT"     + halfWidth;

        ret = codegenCallExpr(fname.c_str(),
                              codegenNeg(codegenUseGlobal(maxFloat)),
                              codegenNeg(codegenUseGlobal(maxFloat)));

      } else {
        INT_FATAL( t, "cannot do min on supplied type");
      }

    } else {
      INT_FATAL( t, "not arithmetic type");
    }
}
DEFINE_PRIM(MAX) {
    Type* t = call->get(1)->typeInfo();

    if (is_arithmetic_type( t)) {
      if (is_int_type( t)) {
        ret = codegenUseGlobal("MAX_INT" + numToString(get_width(t)));

      } else if (is_uint_type( t)) {
        ret = codegenUseGlobal("MAX_UINT" + numToString(get_width(t)));

      } else if (is_real_type( t)) {
        ret = codegenUseGlobal("MAX_FLOAT" + numToString(get_width(t)));

      } else if (is_imag_type( t)) {
        ret = codegenUseGlobal("MAX_FLOAT" + numToString(get_width(t)));

      } else if (is_complex_type( t)) {
        std::string width     = numToString(get_width(t));
        std::string halfWidth = numToString(get_width(t) / 2);

        std::string fname     = "_chpl_complex" + width;
        std::string maxFloat  = "MAX_FLOAT"     + halfWidth;



        ret = codegenCallExpr(fname.c_str(),
                              codegenUseGlobal(maxFloat),
                              codegenUseGlobal(maxFloat));
      } else {
        INT_FATAL( t, "cannot do max on supplied type");
      }

    } else {
      INT_FATAL( t, "not arithmetic type");
    }
}

DEFINE_PRIM(SETCID) {
    // get(1) is the object
    // (type=chpl__class_id,
    //  wide=get(1),
    //  local=chpl__cid_<type>,
    //  stype=dtObject->typeInfo(),
    //  sfield=chpl__cid)
    //
    if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_NO_OBJECT)    == true &&
        call->get(1)->typeInfo()->symbol->hasFlag(FLAG_OBJECT_CLASS) == false) {
      // Don't set cid for a no-object class.
      // This should probably be an error in the future.

    } else {
      Type* classType;

      if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        classType = call->get(1)->typeInfo()->getField("addr")->type;
      } else {
        classType = call->get(1)->typeInfo();
      }

      GenRet ref = codegenFieldCidPtr(call->get(1));

      codegenAssign(ref, codegenUseCid(classType));
    }
}
DEFINE_PRIM(GETCID) {
    INT_ASSERT(call->get(1)->typeInfo() != dtNil);

    if (call->get(1)->getValType()->typeInfo()->symbol->hasFlag(FLAG_NO_OBJECT)   == true &&
        call->get(1)->getValType()->typeInfo()->symbol->hasFlag(FLAG_OBJECT_CLASS) == false) {
      INT_ASSERT(0);
    }

    GenRet ref = codegenFieldCidPtr(call->get(1));

    ret = codegenValue(ref);
}
DEFINE_PRIM(TESTCID) {
    // get(1) is an object to test, get(2) we just use the type of it.
    INT_ASSERT(call->get(1)->typeInfo() != dtNil);

    if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_NO_OBJECT)   == true &&
        call->get(1)->typeInfo()->symbol->hasFlag(FLAG_OBJECT_CLASS) == false) {
      INT_ASSERT(0);
    }

    GenRet ref = codegenFieldCidPtr(call->get(1));

    ret = codegenEquals(ref, codegenUseCid(call->get(2)->typeInfo()));
}

DEFINE_PRIM(SET_UNION_ID) {
    // get(1)->_uid = get(2)
    GenRet ref = codegenFieldUidPtr(call->get(1));

    codegenAssign(ref, call->get(2));
}
DEFINE_PRIM(GET_UNION_ID) {
    // returns uid from get(1)
    GenRet ref = codegenFieldUidPtr(call->get(1));

    ret = codegenValue(ref);
}

DEFINE_PRIM(SET_SVEC_MEMBER) {
    // set tuple base=get(1) at index=get(2) to value=get(3)
    GenRet ptr = codegenElementPtr(call->get(1), call->get(2));
    GenRet val = call->get(3);
    // BHARSH TODO: 'getSvecSymbol' may also be useful here...
    if (call->get(3)->isRefOrWideRef() && !ptr.chplType->symbol->isRefOrWideRef()) {
      val = codegenDeref(val);
    }

    codegenAssign(ptr, val);
}
DEFINE_PRIM(GET_MEMBER) {
    // base=get(1), field symbol=get(2)

    // Invalid AST to use PRIM_GET_MEMBER with a ref field
    INT_ASSERT(!call->get(2)->isRef());

    ret = codegenFieldPtr(call->get(1), call->get(2));

    // Used to only do addrOf if
    // !get(2)->typeInfo()->symbol->hasFlag(FLAG_REF)
    // but that unnaturally depends on the type of the field.
    ret = codegenAddrOf(ret);
}
DEFINE_PRIM(GET_SVEC_MEMBER) {
    // get tuple base=get(1) at index=get(2)
    Type* tupleType = call->get(1)->getValType();

    ret = codegenElementPtr(call->get(1), call->get(2));

    if (tupleType->getField("x0")->type->symbol->hasFlag(FLAG_REF) == false)
      ret = codegenAddrOf(ret);
}
DEFINE_PRIM(SET_MEMBER) {
    // base=get(1), field=get(2), value=get(3)

    // if the field is a ref, and the value is a not ref, invalid AST
    if (call->get(2)->isRef() && !call->get(3)->isRef())
      INT_FATAL("Invalid PRIM_SET_MEMBER ref field with value");

    GenRet obj = call->get(1);
    GenRet ptr = codegenFieldPtr(obj, call->get(2));
    GenRet val = call->get(3);

    if (isHeapAllocatedType(call->get(1)->getValType()))
      ptr.mustPointOutsideOrderIndependentLoop = true;
    else
      ptr.mustPointOutsideOrderIndependentLoop =
        obj.mustPointOutsideOrderIndependentLoop;

    if (call->get(3)->isRefOrWideRef() && !call->get(2)->isRefOrWideRef()) {
      val = codegenDeref(val);
    }

    codegenAssign(ptr, val);
}

DEFINE_PRIM(CHECK_NIL) {
    GenRet ptr = call->get(1);

    ptr = maybeConvertToLocalPointer(call->get(1), ptr);

    codegenCall("chpl_check_nil",
                ptr,
                call->get(2),
                call->get(3));
}
DEFINE_PRIM(LOCAL_CHECK) {
    // arguments are (wide ptr, error string, line, function/file)
    GenRet lhs = call->get(1);
    Symbol* lhsType = lhs.chplType->symbol;
    auto immediate = toVarSymbol(toSymExpr(call->get(2))->symbol())->immediate;
    const char* errorStr = immediate->v_string.c_str();

    if (lhsType->hasEitherFlag(FLAG_WIDE_REF, FLAG_WIDE_CLASS) == true) {
      GenRet filename = GenRet(call->get(4));
      GenRet error = GenRet(errorStr);

      GenRet lhs = call->get(1);
      if (call->get(1)->isRef()) {
        lhs = codegenDeref(lhs);
      }

      codegenCall("chpl_check_local",
                  codegenRnode(lhs),
                  call->get(3),
                  filename,
                  error);
    }
}
DEFINE_PRIM(IS_LOCAL) {
    // arguments are (wide ptr, error string, line, function/file)
    GenRet lhs = call->get(1);
    Symbol* lhsType = lhs.chplType->symbol;

    if (lhsType->hasEitherFlag(FLAG_WIDE_REF, FLAG_WIDE_CLASS) == true) {
      GenRet lhs = call->get(1);
      if (call->get(1)->isRef()) {
        lhs = codegenDeref(lhs);
      }

      ret = codegenCallExpr("chpl_is_local",
                            codegenRlocale(lhs));
    }
    else {
      ret = gTrue->codegen();
    }
}

DEFINE_PRIM(GET_SERIAL) {
    ret = codegenCallExpr("chpl_task_getSerial");
}
DEFINE_PRIM(SET_SERIAL) {
    codegenCall("chpl_task_setSerial", codegenValue(call->get(1)));
}
DEFINE_PRIM(GET_DYNAMIC_END_COUNT) {
      CallExpr* rcall = new CallExpr(gGetDynamicEndCount);
      ret = rcall->codegen();
}
DEFINE_PRIM(SET_DYNAMIC_END_COUNT) {
      CallExpr* rcall = new CallExpr(gSetDynamicEndCount, call->get(1)->copy());
      ret = rcall->codegen();
}

static bool isCStringImmediate(Symbol* sym) {
  if (sym->isImmediate()) {
    VarSymbol* varSym = toVarSymbol(sym);
    return varSym->immediate->const_kind == CONST_KIND_STRING &&
           varSym->immediate->string_kind == STRING_KIND_C_STRING;
  }

  return false;
}

static GenRet codegenGPUInitKernelCfg(CallExpr* call, const char* fnName) {
  int curArg = 1;
  std::vector<GenRet> args;
  Symbol* kernelFn = toSymExpr(call->get(curArg++))->symbol();
  if (FnSymbol* fn = toFnSymbol(kernelFn)) {
    args.push_back(new_CStringSymbol(fn->cname));
  }
  else if (isCStringImmediate(kernelFn)) {
    args.push_back(kernelFn->codegen());
  }
  else {
    INT_FATAL("Unexpected value in kernel initialization primitive");
  }

  for ( ; curArg<=call->numActuals() ; curArg++) {
    args.push_back(call->get(curArg)->codegen());
  }

  return codegenCallExprWithArgs(fnName, args);
}

DEFINE_PRIM(GPU_KERNEL_LAUNCH) {
  ret = codegenCallExpr("chpl_gpu_launch_kernel", call->get(1)->codegen());
}

DEFINE_PRIM(GPU_INIT_KERNEL_CFG_3D) {
  const char* fnName = "chpl_gpu_init_kernel_cfg_3d";

  ret = codegenGPUInitKernelCfg(call, fnName);
}

DEFINE_PRIM(GPU_INIT_KERNEL_CFG) {
  const char* fnName = "chpl_gpu_init_kernel_cfg";

  ret = codegenGPUInitKernelCfg(call, fnName);
}

DEFINE_PRIM(GPU_DEINIT_KERNEL_CFG) {
  ret = codegenCallExpr("chpl_gpu_deinit_kernel_cfg", call->get(1)->codegen());
}

DEFINE_PRIM(GPU_ARG) {
  std::vector<GenRet> args;

  // the config
  args.push_back(call->get(1)->codegen());

  SymExpr* kindSE = toSymExpr(call->get(3));
  INT_ASSERT(kindSE);

  Immediate* imm = getSymbolImmediate(kindSE->symbol());
  int8_t kind = imm->v_int8;

  if (kind & GpuArgKind::ADDROF) {
    args.push_back(codegenAddrOf(codegenValuePtr(call->get(2))));
  }
  else {
    args.push_back(call->get(2)->codegen());
  }

  const char* fnName;
  if (kind & GpuArgKind::OFFLOAD) {
    fnName = "chpl_gpu_arg_offload";
    args.push_back(codegenSizeof(call->get(2)->typeInfo()->getValType()));

    // can OFFLOAD and REDUCE be set at the same time?
    INT_ASSERT((kind & GpuArgKind::REDUCE) == 0);
  }
  else if (kind & GpuArgKind::REDUCE) {
    fnName = "chpl_gpu_arg_reduce";
    args.push_back(codegenSizeof(call->get(2)->typeInfo()->getValType()));
    args.push_back(call->get(4)->codegen());

  } else if (kind & GpuArgKind::HOST_REGISTER) {
    fnName = "chpl_gpu_arg_host_register";
    args.push_back(codegenSizeof(call->get(2)->typeInfo()->getValType()));

  } else {
    fnName = "chpl_gpu_arg_pass";
  }

  ret = codegenCallExprWithArgs(fnName, args);
}

DEFINE_PRIM(GPU_PID_OFFLOAD) {
  auto cfg = call->get(1);
  auto pid = call->get(2);
  auto size = call->get(3);

  ret = codegenCallExpr("chpl_gpu_pid_offload", cfg->codegen(),
                        pid->codegen(), size->codegen());
}

DEFINE_PRIM(GPU_BLOCK_REDUCE) {
  int curArg = 1;
  auto fnNameExpr = call->get(curArg++);
  auto threadData = call->get(curArg++);
  auto buffer = call->get(curArg++);
  auto blockSize = call->get(curArg++);


  VarSymbol* fnNameSym = toVarSymbol(toSymExpr(fnNameExpr)->symbol());
  INT_ASSERT(fnNameSym);
  std::string fnName = fnNameSym->immediate->string_value();

  // append typename
  fnName += "_" + std::string(threadData->typeInfo()->symbol->cname);

  // specialize for a given block size if statically available
  if (Immediate* imm = getSymbolImmediate(toSymExpr(blockSize)->symbol())) {
    fnName += "_" + std::to_string(imm->int_value());
  }

  ret = codegenCallExpr(fnName.c_str(), threadData->codegen(),
                        buffer->codegen());
}

DEFINE_PRIM(GPU_REDUCE_WRAPPER) {
  // GPU_REDUCE_WRAPPER(c"function name without type", data type, ...)
  // "..." args are directly passed to the runtime
  int curArg = 1;
  VarSymbol* fnNameSym = toVarSymbol(toSymExpr(call->get(curArg++))->symbol());
  INT_ASSERT(isCStringImmediate(fnNameSym));

  TypeSymbol *redTypeSym = toTypeSymbol(toSymExpr(call->get(curArg++))->symbol());
  INT_ASSERT(redTypeSym);

  std::vector<GenRet> args;
  for ( ; curArg<=call->numActuals() ; curArg++) {
    args.push_back(call->get(curArg)->codegen());
  }

  std::string fnName = std::string(fnNameSym->immediate->string_value());
  fnName += "_";
  fnName += std::string(redTypeSym->cname);

  ret = codegenCallExprWithArgs(fnName.c_str(), args);
}

DEFINE_PRIM(GPU_THREADIDX_X) { ret = codegenCallExpr("chpl_gpu_getThreadIdxX"); }
DEFINE_PRIM(GPU_THREADIDX_Y) { ret = codegenCallExpr("chpl_gpu_getThreadIdxY"); }
DEFINE_PRIM(GPU_THREADIDX_Z) { ret = codegenCallExpr("chpl_gpu_getThreadIdxZ"); }
DEFINE_PRIM(GPU_BLOCKIDX_X)  { ret = codegenCallExpr("chpl_gpu_getBlockIdxX"); }
DEFINE_PRIM(GPU_BLOCKIDX_Y)  { ret = codegenCallExpr("chpl_gpu_getBlockIdxY"); }
DEFINE_PRIM(GPU_BLOCKIDX_Z)  { ret = codegenCallExpr("chpl_gpu_getBlockIdxZ"); }
DEFINE_PRIM(GPU_BLOCKDIM_X)  { ret = codegenCallExpr("chpl_gpu_getBlockDimX"); }
DEFINE_PRIM(GPU_BLOCKDIM_Y)  { ret = codegenCallExpr("chpl_gpu_getBlockDimY"); }
DEFINE_PRIM(GPU_BLOCKDIM_Z)  { ret = codegenCallExpr("chpl_gpu_getBlockDimZ"); }
DEFINE_PRIM(GPU_GRIDDIM_X)   { ret = codegenCallExpr("chpl_gpu_getGridDimX"); }
DEFINE_PRIM(GPU_GRIDDIM_Y)   { ret = codegenCallExpr("chpl_gpu_getGridDimY"); }
DEFINE_PRIM(GPU_GRIDDIM_Z)   { ret = codegenCallExpr("chpl_gpu_getGridDimZ"); }

DEFINE_PRIM(GPU_ALLOC_SHARED) {
#ifdef HAVE_LLVM
  GenInfo* info = gGenInfo;
  int bytesToAlloc = toVarSymbol(toSymExpr(call->get(1))->symbol())->immediate->int_value();

  // Create a type for the shared array.
  llvm::Type* elementType = llvm::Type::getInt8Ty(gContext->llvmContext());
  auto* arrayType = llvm::ArrayType::get(elementType, bytesToAlloc);

  // Allocate the shared array in GPU shared memory.
  llvm::GlobalVariable* sharedArray = new llvm::GlobalVariable(
      *info->module, arrayType, false, llvm::GlobalValue::InternalLinkage,
      llvm::UndefValue::get(arrayType), "gpuSharedMemory", nullptr,
      llvm::GlobalValue::NotThreadLocal, 3, false);
  trackLLVMValue(sharedArray);

  // Get a void* pointer to the shared array.
  llvm::Type* voidPtrType = getPointerType(gContext->llvmContext(), 3);
  llvm::Value* sharedArrayPtr = gGenInfo->irBuilder->CreateBitCast(sharedArray, voidPtrType, "sharedArrayPtr");
  trackLLVMValue(sharedArrayPtr);

  ret.val = sharedArrayPtr;
  ret.isLVPtr = GEN_VAL;
  ret.chplType = dtCVoidPtr;
#endif
}

DEFINE_PRIM(GPU_SYNC_THREADS) {
  if(!gCodegenGPU) {
    return;
  }
#ifdef HAVE_LLVM
  /* One approach to generating the barrier to call the
   llvm intrinsic function for it, however
   this approach ended up causing issues for us likely due this bug
   reported here:

    https://github.com/llvm/llvm-project/issues/58626

   So instead we call a function in our runtime that includes inline
   assembly to make the barrier call. If the llvm issue is resolved
   we may want to change this back:

  Type *chplReturnType = dtVoid;
  llvm::Function *fun = llvm::Intrinsic::getDeclaration(gGenInfo->module,
    llvm::Intrinsic::nvvm_barrier0);
  ret.val = gGenInfo->irBuilder->CreateCall(fun);
  trackLLVMValue(ret.val);
  ret.isLVPtr = GEN_VAL;
  ret.chplType = chplReturnType;*/

  ret = codegenCallExpr("chpl_gpu_force_sync");
#endif
}

DEFINE_PRIM(GET_REQUESTED_SUBLOC) { ret = codegenCallExpr("chpl_task_getRequestedSubloc"); }

static void codegenPutGet(CallExpr* call, GenRet &ret) {
    // args are:
    //  localvar, locale, sublocale(opt), remote addr, get(4|5)==size, line, file
    //                                                 get(4|5)==len  for array_get/put
    const char* fn;
    std::vector<GenRet> args;
    TypeSymbol* dt;
    bool isget = true;

    // we actually have 5 arguments with sublocale, but than line and file info
    // are added.
    bool hasSubloc = (call->numActuals() == 7);
    bool useGpuVersion = usingGpuLocaleModel() && hasSubloc;

    if (call->primitive->tag == PRIM_CHPL_COMM_GET ||
        call->primitive->tag == PRIM_CHPL_COMM_ARRAY_GET) {
      if (useGpuVersion) {
        fn = "chpl_gen_comm_get_from_subloc";
      }
      else {
        fn = "chpl_gen_comm_get";
      }
    } else {
      if (useGpuVersion) {
        fn = "chpl_gen_comm_put_to_subloc";
      }
      else {
        fn = "chpl_gen_comm_put";
      }
      isget = false;
    }

    int curArgIdx = 1;

    Expr* curArg = call->get(curArgIdx++);
    INT_ASSERT(curArg, curArg->isRefOrWideRef() ||
                       curArg->getValType()->symbol->hasFlag(FLAG_DATA_CLASS));

    GenRet localAddr = codegenValuePtr(curArg);

    // destination data array
    if (curArg->isWideRef()) {
      Symbol* sym = curArg->typeInfo()->getField("addr", true);

      INT_ASSERT(sym);
      dt        = sym->typeInfo()->getValType()->symbol;
      localAddr = codegenRaddr(localAddr);

    } else {
      dt = curArg->typeInfo()->getValType()->symbol;

      if (curArg->typeInfo()->symbol->hasFlag(FLAG_REF)) {
        localAddr = codegenDeref(localAddr);
      }
    }

    localAddr = codegenCastToVoidStar(localAddr);
    args.push_back(localAddr);

    curArg = call->get(curArgIdx++);
    INT_ASSERT(curArg, LOCALE_ID_TYPE);

    GenRet locale = codegenValueMaybeDeref(curArg);;
    args.push_back(locale);


    if (hasSubloc) {
      // if the call has subloc argument, first capture/consume it
      curArg = call->get(curArgIdx++);
      INT_ASSERT(curArg, SUBLOC_ID_TYPE);

      if (useGpuVersion) {
        // we only create an argument if we're using the GPU version of the
        // runtime call
        GenRet subloc = codegenValueMaybeDeref(curArg);
        args.push_back(subloc);
      }
    }
    else {
      // the call doesn't have a subloc argument
      if (useGpuVersion) {
        // but we need to use the GPU version, pass `c_sublocid_none`
        GenRet subloc = codegenUseGlobal("c_sublocid_none");
        args.push_back(subloc);
      }
    }


    // source data array
    curArg = call->get(curArgIdx++);
    INT_ASSERT(curArg, curArg->isRefOrWideRef() ||
                       curArg->getValType()->symbol->hasFlag(FLAG_DATA_CLASS));

    GenRet   remoteAddr = curArg;


    if        (curArg->isWideRef()   == true)  {
      remoteAddr = codegenRaddr(remoteAddr);

    } else if (curArg->isRef()        == false) {
      remoteAddr = codegenAddrOf(remoteAddr);
    }

    args.push_back(remoteAddr);

    curArg = call->get(curArgIdx++);
    INT_ASSERT(curArg, is_int_type(curArg->getValType()) ||
               is_uint_type(curArg->getValType()));

    GenRet len = codegenValueMaybeDeref(curArg);
    GenRet size;

    if (call->primitive->tag == PRIM_CHPL_COMM_ARRAY_PUT ||
        call->primitive->tag == PRIM_CHPL_COMM_ARRAY_GET) {
      GenRet eltSize = codegenSizeof(dt->typeInfo());

      size = codegenMul(eltSize, len);
    } else {
      size = len;
    }

    args.push_back(size);
    args.push_back(genCommID(gGenInfo));
    args.push_back(call->get(curArgIdx++));
    args.push_back(call->get(curArgIdx++));

    if (!fLLVMWideOpt) {
      codegenCallWithArgs(fn, args);
    } else {
      // Figure out the locale-struct value to put into the wide address
      // (instead of just using the node number)
      GenRet lc = codegenLocaleForNode(locale);

      remoteAddr = codegenWideAddr(lc, remoteAddr);

      if (remoteAddr.isLVPtr == GEN_WIDE_PTR)
        remoteAddr = codegenAddrOf(remoteAddr);

      if (localAddr.isLVPtr == GEN_PTR)
        localAddr = codegenAddrOf(localAddr);

      if (localAddr.isLVPtr == GEN_WIDE_PTR)
        localAddr = codegenRaddr(localAddr);

      if (isget) {
        codegenCallMemcpy(localAddr,
                          remoteAddr,
                          size,
                          NULL);
      } else {
        codegenCallMemcpy(remoteAddr,
                          localAddr,
                          size,
                          NULL);
      }
    }
}
DEFINE_PRIM(CHPL_COMM_GET) {
  codegenPutGet(call, ret);
}
DEFINE_PRIM(CHPL_COMM_PUT) {
  codegenPutGet(call, ret);
}
DEFINE_PRIM(CHPL_COMM_ARRAY_GET) {
  codegenPutGet(call, ret);
}
DEFINE_PRIM(CHPL_COMM_ARRAY_PUT) {
  codegenPutGet(call, ret);
}
DEFINE_PRIM(CHPL_COMM_REMOTE_PREFETCH) {
    // args are:
    //   locale, remote addr, get(3)==size, line, file

    // Get the locale
    GenRet locale = codegenValueMaybeDeref(call->get(1));

    // source data array
    GenRet   remoteAddr = call->get(2);
    SymExpr* sym        = toSymExpr(call->get(2));

    INT_ASSERT(sym);

    if (sym->isWideRef()) {
      remoteAddr = codegenRaddr(remoteAddr);

    } else if (sym->isRef() == false) {
      remoteAddr = codegenAddrOf(remoteAddr);
    }

    GenRet len = codegenValueMaybeDeref(call->get(3));

    codegenCall("chpl_gen_comm_prefetch",
                locale,
                remoteAddr,
                len,
                genCommID(gGenInfo),
                call->get(4),
                call->get(5));
}

// Strided versions of get and put
static void codegenPutGetStrd(CallExpr* call, GenRet &ret) {
    // args are: local addr, dststr addr,
    //           locale, sublocale, remote addr, srcstr addr,
    //           count addr, strlevels,
    //           line number, function id
    const char* fn;
    TypeSymbol* dt;

    if (call->primitive->tag == PRIM_CHPL_COMM_GET_STRD) {
      fn = "chpl_gen_comm_get_strd";
    } else {
      fn = "chpl_gen_comm_put_strd";
    }

    auto destData = call->get(1);
    GenRet localAddr = codegenValuePtr(destData);

    // destination data array
    if (destData->isWideRef()) {
      Symbol* sym = destData->typeInfo()->getField("addr", true);

      INT_ASSERT(sym);
      dt        = sym->typeInfo()->getValType()->symbol;
      localAddr = codegenRaddr(localAddr);
    } else {
      dt = destData->typeInfo()->getValType()->symbol;

      if (destData->typeInfo()->symbol->hasFlag(FLAG_REF)) {
        localAddr = codegenDeref(localAddr);
      }
    }

    // destination strides local array
    auto destStride = call->get(2);
    GenRet dststr = codegenValuePtr(destStride);

    if (destStride->isWideRef()) {
      Symbol* sym = destStride->typeInfo()->getField("addr", true);

      INT_ASSERT(sym);

      dststr = codegenRaddr(dststr);
    } else if (destStride->typeInfo()->symbol->hasFlag(FLAG_REF)) {
      dststr = codegenDeref(dststr);
    }

    // locale id
    GenRet locale = codegenValueMaybeDeref(call->get(3));

    // sublocale id
    GenRet subloc = codegenValueMaybeDeref(call->get(4));

    // source data array
    auto srcExpr = call->get(5);
    GenRet   remoteAddr = srcExpr;

    if (srcExpr->isWideRef() == true) {
      remoteAddr = codegenRaddr(remoteAddr);
    } else if (srcExpr->isRef() == false) {
      remoteAddr = codegenAddrOf(remoteAddr);
    }

    // source strides local array
    auto srcStrideExpr = call->get(6);
    GenRet srcstr = codegenValuePtr(srcStrideExpr);

    if (srcStrideExpr->isWideRef()) {
      Symbol* sym = srcStrideExpr->typeInfo()->getField("addr", true);

      INT_ASSERT(sym);

      srcstr = codegenRaddr(srcstr);
    } else {
      if (srcStrideExpr->typeInfo()->symbol->hasFlag(FLAG_REF)) {
        srcstr = codegenDeref(srcstr);
      }
    }

    // count local array
    auto countExpr = call->get(7);
    GenRet count = codegenValuePtr(countExpr);

    if (countExpr->isWideRef()) {
      Symbol* sym = countExpr->typeInfo()->getField("addr", true);

      INT_ASSERT(sym);

      count = codegenRaddr(count);
    } else if (countExpr->typeInfo()->symbol->hasFlag(FLAG_REF)) {
      count = codegenDeref(count);
    }

    // stridelevels
    GenRet stridelevels = codegenValueMaybeDeref(call->get(8));

    auto lineno = call->get(9);
    auto fnID = call->get(10);

    // eltSize
    GenRet eltSize = codegenSizeof(dt->typeInfo());

    std::vector<GenRet> args;

    args.push_back(codegenCastToVoidStar(localAddr));
    args.push_back(codegenCastToVoidStar(dststr));
    args.push_back(locale);
    args.push_back(subloc);
    args.push_back(remoteAddr);
    args.push_back(codegenCastToVoidStar(srcstr));
    args.push_back(codegenCastToVoidStar(count));
    args.push_back(stridelevels);
    args.push_back(eltSize);
    args.push_back(genCommID(gGenInfo));
    args.push_back(lineno);
    args.push_back(fnID);

    codegenCallWithArgs(fn, args);
}
DEFINE_PRIM(CHPL_COMM_PUT_STRD) {
  codegenPutGetStrd(call, ret);
}
DEFINE_PRIM(CHPL_COMM_GET_STRD) {
  codegenPutGetStrd(call, ret);
}

DEFINE_PRIM(SIZEOF_BUNDLE) {
    Type*  type = call->get(1)->typeInfo();
    GenRet size;

      // If wide, get the value type.
    if (type->symbol->hasFlag(FLAG_WIDE_CLASS) ||
        call->get(1)->isWideRef())
      type = toAggregateType(type)->getField("addr", true)->typeInfo();


    // If Chapel class or record
    if (AggregateType* ct = toAggregateType(type)) {
      size = codegenSizeof(ct->classStructName(true));
    } else {
      size = codegenSizeof(type);
    }

    ret = size;
}

DEFINE_PRIM(SIZEOF_DDATA_ELEMENT) {
    Type*  type = call->get(1)->typeInfo();
    GenRet size;
    if (type->symbol->hasFlag(FLAG_WIDE_CLASS) == true) {
      size = codegenSizeof(getDataClassType(type->getField("addr")->
                                            type->symbol)->typeInfo());
    } else {
      size = codegenSizeof(getDataClassType(type->symbol)->typeInfo());
    }
    ret = size;
}

DEFINE_PRIM(CAST) {
    if (call->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) ||
        call->isWideRef()) {

      GenRet tmp = call->get(2);

      ret = codegenWideAddrWithAddr(tmp,
                                    codegenCast(call->get(1)->typeInfo(),
                                                codegenRaddr(tmp)));
    } else {
      Type* dst = call->get(1)->typeInfo();
      Type* src = call->get(2)->typeInfo();

      GenRet srcGen = call->get(2);

      // 2016-12-08
      // Denormalize might move the reference in an implicit dereference
      // (move non-ref, ref) into a PRIM_CAST. This would then be a cast from
      // a reference type to a non-reference type, which doesn't really make
      // sense in today's compiler. When we see such a case, assume that we
      // should deref the 'src'.
      if (src->symbol->hasFlag(FLAG_REF) &&
          dst->symbol->hasFlag(FLAG_REF) == false) {
        srcGen = codegenDeref(srcGen);
        src = src->getValType();
      }

      // C standard promotes small ints to full int when they are involved in
      // arithmetic operations. When we don't denormalize the AST, small integer
      // arithmetic is always assigned to a temporary of the suitable size,
      // which truncates the integer appropriately. OTOH, if we denormalize the
      // AST then we have to make sure that are cast to appropriate size.
      //
      // TODO We use a wide brush by casting all integer operations. It should
      // be enough to cast integers that are smaller than standard C int for
      // target architecture. However, there was no easy way of obtaining that
      // at the time of writing this piece. Engin
      if (dst == src && !(is_int_type(dst) || is_uint_type(dst) ||
                          is_real_type(dst)) ) {
        ret = srcGen;

      } else if ((is_int_type(dst) || is_uint_type(dst)) && src == dtTaskID) {
        GenRet v = codegenValue(srcGen);

        // cast like this: (type) (intptr_t) v
        ret = codegenCast(call->typeInfo(), codegenCast("intptr_t", v));

      } else if (isRecord(call->typeInfo()) || isUnion(call->typeInfo())) {
        INT_FATAL("TODO - don't like type-punning record/union");

      } else if (src->symbol->hasFlag(FLAG_WIDE_CLASS) &&
          isCVoidPtr(call->typeInfo())) {
        // Special case: If we are casting a wide-ptr to a c_ptr(void) we need to ensure
        // that we perform the cast on the actual address portion of the wide-ptr. LouisJenkinsCS
        ret = codegenCast(call->typeInfo(), codegenRaddr(srcGen));

      } else {
        GenRet v = codegenValue(srcGen);

        ret = codegenCast(call->typeInfo(), v);
      }
    }
}
DEFINE_PRIM(DYNAMIC_CAST) {
    if (call->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
      INT_FATAL(call, "wide class dynamic cast is not normal");

    GenRet tmp   = codegenFieldCidPtr(call->get(2));
    GenRet value = codegenValue(tmp);
    GenRet ok    = codegenDynamicCastCheck(value, call->typeInfo());
    GenRet cast  = codegenCast(call->typeInfo(), codegenValue(call->get(2)));
    GenRet nul   = codegenCast(call->typeInfo(), codegenNullPointer());

    ret = codegenTernary(ok, cast, nul);
}

DEFINE_PRIM(STACK_ALLOCATE_CLASS) {
    AggregateType* at = toAggregateType(call->get(1)->typeInfo());
    const char* struct_name = at->classStructName(true);
    int alignment = 0;  // ignored for C codegen
#ifdef HAVE_LLVM
    if (! gGenInfo->cfile) alignment = at->symbol->getLLVMStructureAlignment();
#endif
    GenRet tmp = createTempVar(struct_name, alignment);

    ret = codegenCast(at, codegenAddrOf(tmp));
}

static
void codegenCallMemset(GenRet dest, Type* type) {
  GenInfo *info = gGenInfo;

  GenRet size = codegenSizeof(type);

  // Must call with real pointer arguments (not lvalue)
  INT_ASSERT(dest.isLVPtr == GEN_VAL);
  // And also above call should generate a value
  INT_ASSERT(size.isLVPtr == GEN_VAL);

  if (info->cfile) {
    GenRet zero = codegenZero32();
    codegenCall("memset", dest, zero, size);
  } else {
#ifdef HAVE_LLVM
    llvm::ConstantInt* zero = info->irBuilder->getIntN(8, 0);
    llvm::CallInst* memset = info->irBuilder->CreateMemSet(dest.val, zero, size.val, llvm::Align(1));
    trackLLVMValue(memset);
#endif
  }
}

DEFINE_PRIM(ZERO_VARIABLE) {
    SymExpr* se = toSymExpr(call->get(1));
    INT_ASSERT(se);
    Symbol* sym = se->symbol();
    Type* type = sym->getValType();
    GenRet dest = codegenAddrOf(se);

    codegenCallMemset(dest, type);
}

DEFINE_PRIM(REGISTER_GLOBAL_VAR) {
    GenRet idx          = codegenValue(call->get(1));
    GenRet var          = call->get(2);
    GenRet ptr_wide_ptr = codegenAddrOf(var);

#ifdef HAVE_LLVM
    if( fLLVMWideOpt ) {
      // With fLLVMWideOpt, ptr_wide_ptr is actually pointing
      // to a global pointer at this point, and we can't call
      // a C function on a global type (since it won't exist
      // by the time we are linking - it will have been lowered
      // to a wide type). So add the call to convert it to
      // wide (which makes the IR type check but will just
      // get removed in the eventual code).
      llvm::Type* ptr_wide_ptr_ty = ptr_wide_ptr.val->getType();

      // call GLOBAL_FN_GLOBAL_TO_WIDE dummy function
      llvm::Function* fn = getGlobalToWideFn(gGenInfo->module,
                                             &gGenInfo->globalToWideInfo,
                                             ptr_wide_ptr_ty);

      INT_ASSERT(fn);

      ptr_wide_ptr.val = gGenInfo->irBuilder->CreateCall(fn, ptr_wide_ptr.val);
      trackLLVMValue(ptr_wide_ptr.val);
    }
#endif

    codegenCall("chpl_comm_register_global_var",
                idx,
                codegenCast("ptr_wide_ptr_t", ptr_wide_ptr));
}
DEFINE_PRIM(BROADCAST_GLOBAL_VARS) {
    codegenCall("chpl_comm_broadcast_global_vars", call->get(1));
}
DEFINE_PRIM(PRIVATE_BROADCAST) {
    codegenCall("chpl_comm_broadcast_private",
                call->get(1),
                codegenSizeof(call->get(2)->typeInfo()));
}

DEFINE_PRIM(INT_ERROR) {
    codegenCall("chpl_internal_error",
                new_CStringSymbol("compiler generated error"));
}
DEFINE_PRIM(STRING_COPY) {
    GenRet cpyFrom = call->get(1)->codegen();

    if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
      cpyFrom.isLVPtr = GEN_VAL; // Prevent &(char*) syntax.
      ret             = codegenCallExpr("chpl_wide_string_copy",
                                        cpyFrom,
                                        call->get(2),
                                        call->get(3));
    } else
      ret = call->codegenBasicPrimitiveExpr();
}

static GenRet maybeConvertToLocalPointer(Expr* expr, GenRet& act) {
  GenRet ret;

  bool isRefToWide = false;
  if (expr->isRef() &&
      act.chplType->getValType()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
    isRefToWide = true;
  }

  if (act.chplType->symbol->hasEitherFlag(FLAG_WIDE_REF, FLAG_WIDE_CLASS) ||
      isRefToWide) {
    // Get the local address.
    // Assume that we have already tested to ensure that this wide pointer
    // is local.  That is, caller should have called chpl_check_local.
    if (isRefToWide) act = codegenDeref(act);
    ret = codegenRaddr(act);
  } else {
    ret = codegenValue(expr);
  }

  return ret;
}

DEFINE_PRIM(CAST_TO_VOID_STAR) {
  GenRet act = call->get(1);
  GenRet ptr = maybeConvertToLocalPointer(call->get(1), act);
  ret = codegenCastToVoidStar(ptr);
}

static GenRet codegenFunctionTypeCast(Expr* val, FunctionType* ft2) {
  INT_FATAL(val, "Not implemented yet!");
  GenRet ret;
  return ret;
}

DEFINE_PRIM(CAST_TO_TYPE) {
  INT_ASSERT(call->numActuals() == 2);

  auto se = toSymExpr(call->get(2));
  INT_ASSERT(se);
  auto ts = toTypeSymbol(se->symbol());
  INT_ASSERT(ts);

  if (auto ft = toFunctionType(ts->type)) {
    ret = codegenFunctionTypeCast(call->get(1), ft);
  } else {
    GenRet act = call->get(1);
    GenRet ptr = maybeConvertToLocalPointer(call->get(1), act);
    ret = codegenCast(ts->cname, ptr);
  }
}

DEFINE_BASIC_PRIM(RT_ERROR)
DEFINE_BASIC_PRIM(RT_WARNING)
DEFINE_BASIC_PRIM(RT_GPU_HALT)
DEFINE_BASIC_PRIM(START_RMEM_FENCE)
DEFINE_BASIC_PRIM(FINISH_RMEM_FENCE)

DEFINE_PRIM(ASSERT_ON_GPU) {
  ret = call->codegenBasicPrimitiveExpr();
}

DEFINE_PRIM(NEW_PRIV_CLASS) {
    GenRet arg = call->get(1);
    GenRet pid = codegenValue(call->get(2));

    if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
      arg = codegenRaddr(arg);

    codegenCall("chpl_newPrivatizedClass", arg, pid);
}

DEFINE_PRIM(FTABLE_CALL) {
    //
    // indirect function call via a function pointer
    //
    GenRet index = codegenValue(call->get(1));
    GenRet fngen;

    LlvmFunctionTypePtr fnTy = nullptr;

    // Generate a cast based upon the arguments.
    if (gGenInfo->cfile){
      std::string str = "((void(*)(";

      GenRet arg = call->get(2);

      str += arg.chplType->symbol->cname;

      if (argMustUseCPtr(arg.chplType))
        str += "*";

      str += ",";
      str += call->get(3)->typeInfo()->symbol->cname;

      if (argMustUseCPtr(call->get(3)->typeInfo()))
        str += "*";

      str += "))*chpl_ftable[" + index.c + "])";

      fngen.c = str;

    } else {
#ifdef HAVE_LLVM
      GenRet             ftable = gGenInfo->lvt->getValue("chpl_ftable");
      auto global = llvm::cast<llvm::GlobalVariable>(ftable.val);
      GenRet fnPtrT = codegenTypeByName("chpl_fn_p");
      llvm::Type*  genericFnPtr = fnPtrT.type;
      INT_ASSERT(genericFnPtr);
      llvm::Value*       fnPtrPtr;
      llvm::Instruction* fnPtr;
      llvm::Value*       GEPLocs[2];

      GEPLocs[0] = llvm::Constant::getNullValue(llvm::IntegerType::getInt64Ty(gGenInfo->module->getContext()));
      GEPLocs[1] = index.val;
      fnPtrPtr   = createInBoundsGEP(global->getValueType(),
                                     ftable.val, GEPLocs);
      fnPtr      = gGenInfo->irBuilder->CreateLoad(genericFnPtr, fnPtrPtr);
      trackLLVMValue(fnPtr);

      // Generate an LLVM function type based upon the arguments.
      std::vector<llvm::Type*> argumentTypes;
      llvm::Type*              returnType;

      returnType = llvm::Type::getVoidTy(gGenInfo->module->getContext());

      llvm::Type*              argt   = NULL;

      argt = call->get(2)->typeInfo()->codegen().type;

      if (argMustUseCPtr(call->get(2)->typeInfo()))
        argt = llvm::PointerType::getUnqual(argt);

      argumentTypes.push_back(argt);

      argt = call->get(3)->typeInfo()->codegen().type;

      if (argMustUseCPtr(call->get(3)->typeInfo()))
        argt = llvm::PointerType::getUnqual(argt);

      argumentTypes.push_back(argt);

      fnTy = llvm::FunctionType::get(returnType,
                                     argumentTypes,
                                     /* is var arg */ false);

      // OK, now cast to the fnTy.
      fngen.val = gGenInfo->irBuilder->CreateBitCast(fnPtr,
                                                    llvm::PointerType::getUnqual(fnTy));
      trackLLVMValue(fngen.val);
#endif
    }

    std::vector<GenRet> args;
    GenRet              arg = call->get(2);

    if (argMustUseCPtr(arg.chplType) && !call->get(2)->isRef())
      arg = codegenLocalAddrOf(arg);

    args.push_back(arg);

    arg = call->get(3);

    if (argMustUseCPtr(call->get(3)->typeInfo()))
      arg = codegenLocalAddrOf(arg);

    args.push_back(arg);

    ret = codegenCallExprInner(fngen, args, nullptr, nullptr, fnTy, true);
}
DEFINE_PRIM(VIRTUAL_METHOD_CALL) {
    GenRet    fnPtr;
    GenRet    index;
    FnSymbol* fn        = NULL;
    int       startArgs = 3;    // Where actual arguments begin.
    SymExpr*  se        = toSymExpr(call->get(1));  // the function symbol

    INT_ASSERT(se);
    fn = toFnSymbol(se->symbol());
    INT_ASSERT(fn);

    {
      GenRet  i          = codegenValue(call->get(2));    // the cid
      int64_t fnId       = virtualMethodMap.get(fn);
      GenRet j           = new_IntSymbol(fnId,    INT_SIZE_64);
      GenRet maxVMTConst = new_IntSymbol(gMaxVMT, INT_SIZE_64);

      INT_ASSERT(gMaxVMT >= 0);

      // indexExpr = maxVMT * classId + fnId
      index = codegenAdd(codegenMul(maxVMTConst, i), j);
    }

    if (gGenInfo->cfile){
      fnPtr.c = std::string("chpl_vmtable") + "[" + index.c + "]";
    } else {
#ifdef HAVE_LLVM
      GenRet       table = gGenInfo->lvt->getValue("chpl_vmtable");
      auto global = llvm::cast<llvm::GlobalVariable>(table.val);
      llvm::Value* fnPtrPtr;
      GenRet fnPtrT = codegenTypeByName("chpl_fn_p");
      llvm::Type*  genericFnPtr = fnPtrT.type;
      INT_ASSERT(genericFnPtr);
      llvm::Value* GEPLocs[2];
      GEPLocs[0] = llvm::Constant::getNullValue(
          llvm::IntegerType::getInt64Ty(gGenInfo->module->getContext()));
      GEPLocs[1] = index.val;
      fnPtrPtr = createInBoundsGEP(global->getValueType(), table.val, GEPLocs);
      llvm::Instruction* fnPtrV =
        gGenInfo->irBuilder->CreateLoad(genericFnPtr, fnPtrPtr);
      trackLLVMValue(fnPtrV);
      fnPtr.val = fnPtrV;
#endif
    }

    // the function expression to call.
    std::vector<GenRet> args;
    GenRet              fngen = fn->codegenCast(fnPtr);
    int                 i     = startArgs;

    for_formals(arg, fn) {
      args.push_back(call->get(i++));
    }

    ret = codegenCallExprInner(fngen, args, fn, nullptr, nullptr, true);
}

DEFINE_BASIC_PRIM(LOOKUP_FILENAME)

DEFINE_PRIM(INVARIANT_START) {

  GenInfo* info = gGenInfo;
  if (info->cfile) {
    // do nothing for the C backend
  } else {
#ifdef HAVE_LLVM
    GenRet ptr = codegenValue(call->get(1));
    llvm::Value* val = ptr.val;
    codegenInvariantStart(nullptr, val);
#endif
  }
}

#ifdef HAVE_LLVM
static
llvm::MDNode* createMetadataScope(llvm::LLVMContext& ctx,
                                    llvm::MDNode* domain,
                                    const char* name) {

  auto scopeName = llvm::MDString::get(ctx, name);
  auto dummy = llvm::MDNode::getTemporary(ctx, chpl::empty);
  llvm::Metadata* Args[] = {dummy.get(), domain, scopeName};
  auto scope = llvm::MDNode::get(ctx, Args);
  // Remove the dummy and replace it with a self-reference.
  dummy->replaceAllUsesWith(scope);
  return scope;
}

#endif

DEFINE_PRIM(NO_ALIAS_SET) {

  GenInfo* info = gGenInfo;
  if (info->cfile) {
    // do nothing for the C backend
  } else {
#ifdef HAVE_LLVM

    Symbol* sym = toSymExpr(call->get(1))->symbol();

    llvm::LLVMContext &ctx = gContext->llvmContext();

    if (info->noAliasDomain == NULL) {
      auto domainName = llvm::MDString::get(ctx, "Chapel no-alias");
      auto dummy = llvm::MDNode::getTemporary(ctx, chpl::empty);
      llvm::Metadata* Args[] = {dummy.get(), domainName};
      info->noAliasDomain = llvm::MDNode::get(ctx, Args);
      // Remove the dummy and replace it with a self-reference.
      dummy->replaceAllUsesWith(info->noAliasDomain);
    }

    llvm::MDNode *&scope = info->noAliasScopes[sym];
    if (scope == NULL)
      scope = createMetadataScope(ctx, info->noAliasDomain, sym->name);

    // now create a list storing just the scope
    llvm::MDNode *&scopeList = info->noAliasScopeLists[sym];
    if (scopeList == NULL)
      scopeList = llvm::MDNode::get(ctx, scope);

    // now create the no-alias metadata
    llvm::MDNode *&noAliasList = info->noAliasLists[sym];
    if (noAliasList == NULL) {
      llvm::SmallVector<llvm::Metadata*, 6> Args;
      bool first = true;
      for_actuals(actual, call) {
        if (!first) {
          Symbol* noAliasSym = toSymExpr(actual)->symbol();
          llvm::MDNode *&otherScope = info->noAliasScopes[noAliasSym];
          if (otherScope == NULL)
            otherScope = createMetadataScope(ctx, info->noAliasDomain,
                                             noAliasSym->name);

          Args.push_back(otherScope);
        }
        first = false;
      }
      noAliasList = llvm::MDNode::get(ctx, Args);
    }
#endif
  }
}

DEFINE_PRIM(COPIES_NO_ALIAS_SET) {
  GenInfo* info = gGenInfo;
  if (info->cfile) {
    // do nothing for the C backend
  } else {
#ifdef HAVE_LLVM
    Symbol* sym = toSymExpr(call->get(1))->symbol();
    Symbol* otherSym = toSymExpr(call->get(2))->symbol();

    if (info->noAliasScopeLists.count(otherSym) > 0) {
      llvm::MDNode *&scopeList = info->noAliasScopeLists[sym];
      scopeList = info->noAliasScopeLists[otherSym];
    }

    llvm::MDNode *&noAliasList = info->noAliasLists[sym];
    if (info->noAliasLists.count(otherSym) > 0) {
        noAliasList = info->noAliasLists[otherSym];
    }
#endif
  }
}

DEFINE_PRIM(OPTIMIZATION_INFO) {
  // No action required here
}

DEFINE_PRIM(DEBUG_TRAP) {
  GenInfo* info = gGenInfo;
  if (info->cfile) {
    ret = codegenCallExpr("chpl_debugtrap");
  }
  else {
    #ifdef HAVE_LLVM
    ret.val = info->irBuilder->CreateIntrinsic(llvm::Intrinsic::debugtrap, {}, {});
    trackLLVMValue(ret.val);
    #endif
  }
}

DEFINE_PRIM(CONST_ARG_HASH) {
  INT_ASSERT(call->numActuals() == 1);
  INT_ASSERT(call->get(1)->isRefOrWideRef());
  GenRet arg = call->get(1);

  if (call->get(1)->isWideRef()) {
    // Don't send arg in with `&`
    GenRet ptr = codegenValue(arg);
    // Do shallow GET on record
    GenRet val = codegenValue(codegenDeref(ptr));
    // Create temporary that we can pass a pointer to
    GenRet tmp = createTempVarWith(val);
    GenRet addr = codegenAddrOf(tmp);

    Symbol* sym = call->get(1)->typeInfo()->getField("addr", true);
    // Could potentially have the sizeof call inserted at the creation of the
    // primitive
    ret = codegenCallExpr("const_arg_hash", addr,
                          codegenSizeof(sym->typeInfo()->getValType()));
  } else {
    GenRet ptr = codegenValue(arg);

    // Could potentially have the sizeof call inserted at the creation of the
    // primitive
    ret = codegenCallExpr("const_arg_hash", ptr,
                          codegenSizeof(call->get(1)->typeInfo()->getValType()));
  }
}

DEFINE_PRIM(CHECK_CONST_ARG_HASH) {
    INT_ASSERT(call->numActuals() == 5);

    codegenCall("check_const_hash_matches", call->get(1), call->get(2),
                call->get(3), call->get(4), call->get(5));
}

DEFINE_BASIC_PRIM(ASCII)
DEFINE_BASIC_PRIM(SLEEP)
DEFINE_BASIC_PRIM(REAL_TO_INT)
DEFINE_BASIC_PRIM(STRING_INDEX)
DEFINE_BASIC_PRIM(CHPL_EXIT_ANY)
DEFINE_BASIC_PRIM(OBJECT_TO_INT)
DEFINE_BASIC_PRIM(STRING_CONCAT)
DEFINE_BASIC_PRIM(STRING_SELECT)
DEFINE_BASIC_PRIM(STRING_COMPARE)
DEFINE_BASIC_PRIM(STRING_CONTAINS)
DEFINE_BASIC_PRIM(STRING_LENGTH_BYTES)
DEFINE_BASIC_PRIM(STRING_LENGTH_CODEPOINTS)

void CallExpr::registerPrimitivesForCodegen() {
  // The following macros call registerPrimitiveCodegen for
  // the DEFINE_PRIM routines above for each primitive labelled
  // as PRIMITIVE_G (i.e. as needing code generation)
#define PRIMITIVE_G(NAME, str) \
  if (PRIM_ ## NAME != PRIM_UNKNOWN) \
    registerPrimitiveCodegen(PRIM_ ## NAME, codegen ## NAME );
#define PRIMITIVE_R(NAME, str)
#include "chpl/uast/prim-ops-list.h"

#undef PRIMITIVE_G
#undef PRIMITIVE_R
}

GenRet CallExpr::codegenPrimitive() {
  SET_LINENO(this);

  GenRet ret;

  PrimitiveTag tag = primitive->tag;
  void (*codegenFn)(CallExpr*, GenRet&);
  codegenFn = primitive->codegenFn;

  if (tag == PRIM_MOVE) {
    // PRIM_MOVE is the most common by far
    ret = this->codegenPrimMove();
  } else if (tag == PRIM_UNKNOWN) {
    // PRIM_UNKNOWN won't have a codegenFn
    ret = codegenBasicPrimitiveExpr();
  } else if (codegenFn != NULL) {
    // use a registered DEFINE_PRIM function from above
    codegenFn(this, ret);
  } else {
    // otherwise, error
    INT_FATAL(this, "primitive codegen fail; should it still be in the AST?");

    if (gGenInfo->cfile) {
      std::string stmt;

      stmt += "/* ERR ";
      stmt += primitive->name;
      stmt += "*/";

      gGenInfo->cStatements.push_back(stmt);
    }
  }

  if (gGenInfo->cfile       &&
      getStmtExpr()         &&
      getStmtExpr() == this &&
      ret.c.length() > 0)
    gGenInfo->cStatements.push_back(ret.c + ";\n");

  return ret;
}

GenRet CallExpr::codegenPrimMove() {
  GenRet ret;

  const bool LHSRef = get(1)->isRef() || get(1)->isWideRef();
  const bool RHSRef = get(2)->isRef() || get(2)->isWideRef();

  bool mustPointOutsideOrderIndependentLoop = false;

  GenRet specRet;
  if (get(1)->typeInfo() == dtNothing) {
    ret = get(2)->codegen();

  // Is the RHS a primop with special case handling?
  } else if (codegenIsSpecialPrimitive(get(1), get(2), specRet)) {

    // TODO The special treatment for PRIM_GET_REAL and PRIM_GET_IMAG can be
    // removed
    CallExpr* rhsCe = toCallExpr(get(2));
    if(rhsCe->isPrimitive(PRIM_GET_REAL) || rhsCe->isPrimitive(PRIM_GET_IMAG)) {

      if (gGenInfo->cfile) {
        std::string stmt = codegenValue(get(1)).c + " = ";

        stmt += specRet.c;
        stmt += ";\n";

        gGenInfo->cStatements.push_back(stmt);
      } else {
#ifdef HAVE_LLVM
        codegenStoreLLVM(specRet, get(1));
#endif
      }
    }
    else {
      codegenAssign(get(1), specRet);
    }

    mustPointOutsideOrderIndependentLoop =
      specRet.mustPointOutsideOrderIndependentLoop;

  } else if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) == true  &&
             get(2)->getValType()->symbol->hasFlag(FLAG_WIDE_CLASS) == false ) {
    GenRet rhs = get(2);
    if (get(2)->isRef()) {
      rhs = codegenDeref(rhs);
    }
    // At this point, RHS should be a class type.
    INT_ASSERT(isClassOrNil(rhs.chplType));
    GenRet from = codegenWideHere(rhs);
    codegenAssign(get(1), from);
    mustPointOutsideOrderIndependentLoop =
      from.mustPointOutsideOrderIndependentLoop;

  } else if (get(1)->isWideRef() == true &&
             get(2)->isRef() == true) {
    GenRet from = codegenAddrOf(codegenWideHere(get(2)));
    codegenAssign(get(1), from);
    mustPointOutsideOrderIndependentLoop =
      from.mustPointOutsideOrderIndependentLoop;

  } else if (get(1)->isWideRef() == true  &&
             get(2)->isWideRef() == false &&
             get(2)->isRef()     == false) {
    GenRet to_ptr = codegenDeref(get(1));
    GenRet from = get(2);

    codegenAssign(to_ptr, from);
    mustPointOutsideOrderIndependentLoop =
      from.mustPointOutsideOrderIndependentLoop;

  } else if (get(1)->isRef()       == true  &&
             get(2)->isWideRef()   == true)  {
    if (get(1)->getValType() != get(2)->getValType()) {
      GenRet narrowRef   = codegenRaddr(get(2));
      GenRet wideThing   = codegenDeref(narrowRef);
      GenRet narrowThing = codegenWideThingField(wideThing, WIDE_GEP_ADDR);
      GenRet from = codegenAddrOf(narrowThing);

      codegenAssign(get(1), from);
      mustPointOutsideOrderIndependentLoop =
        from.mustPointOutsideOrderIndependentLoop;
    } else {
      GenRet genWide = get(2);
      GenRet from = codegenRaddr(genWide);
      codegenAssign(get(1), from);
      mustPointOutsideOrderIndependentLoop =
        from.mustPointOutsideOrderIndependentLoop;
    }

  } else if (get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) == false &&
             get(1)->isRef()                                      == false &&
             get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) == true)  {
    GenRet from = codegenRaddr(get(2));
    codegenAssign(get(1), from);
    mustPointOutsideOrderIndependentLoop =
      from.mustPointOutsideOrderIndependentLoop;

  } else if (get(1)->isRef()        == true  &&
             get(2)->isRef()        == false) {
    GenRet from = get(2);
    codegenAssign(codegenDeref(get(1)), from);
    mustPointOutsideOrderIndependentLoop =
      from.mustPointOutsideOrderIndependentLoop;
  } else if(!LHSRef && RHSRef) {
    GenRet from = codegenDeref(get(2));
    codegenAssign(get(1), from);
    mustPointOutsideOrderIndependentLoop =
      from.mustPointOutsideOrderIndependentLoop;
  } else {
    GenRet from = get(2);
    codegenAssign(get(1), from);
    mustPointOutsideOrderIndependentLoop =
      from.mustPointOutsideOrderIndependentLoop;
  }

  ret.mustPointOutsideOrderIndependentLoop =
    mustPointOutsideOrderIndependentLoop;
  return ret;
}

/*
 * Some primitives require special attention depending on where they are used.
 * This generally involves casting the result of the primitive to the type of
 * the target.
 *
 * This function is used by codegenPrimMove, CallExpr::codegen(for matching
 * formal/actual types) and codegenPrimitive
 *
 * Returns true if `e` is a special primitive, and sets ret.
 * Returns false and doesn't change ret, otherwise
 */
static bool codegenIsSpecialPrimitive(BaseAST* target, Expr* e, GenRet& ret) {
  bool      retval = false;
  CallExpr* call = toCallExpr(e);

  if(!call) return false;

  if (call->id == breakOnCodegenID)
    debuggerBreakHere();

  if (call->primitive) {
    switch (call->primitive->tag) {
    case PRIM_GET_REAL:
    case PRIM_GET_IMAG: {
      bool isReal = call->primitive->tag == PRIM_GET_REAL;

      if (call->get(1)->isWideRef()) {
        // move(wide_real, prim_get_real(wide_complex));
        // turns into: wide_real.locale = wide_complex.locale;
        //             wide_real.addr = prim_get_real(wide_complex.addr);
        Type*  cplxType = call->get(1)->typeInfo()->getRefType();

        GenRet t1       = createTempVar(cplxType);
        codegenAssign(t1, codegenRaddr(call->get(1)));

        GenRet t2;
        if(target) {
          t2 = createTempVar(target->typeInfo()->getRefType());
        }
        else {
          t2 = createTempVar(cplxType);
        }

        codegenAssign(t2, codegen_prim_get_real(t1, cplxType, isReal));
        ret             = codegenWideAddr(codegenRlocale(call->get(1)),
                                          codegenDeref(t2));

      } else {
        ret = codegen_prim_get_real(call->get(1),
            call->get(1)->typeInfo(), isReal);
      }

      retval = true;
      break;
    }

    case PRIM_DEREF: {
      // BHARSH TODO: What if get(1) for this first branch is not a ref?
      if (call->get(1)->isWideRef() ||
          call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        Type* valueType;

        // BHARSH TODO: It seems odd to use a PRIM_DEREF on a wide class, why do we?
        if (call->get(1)->isWideRef())
          valueType = call->get(1)->getValType();
        else
          valueType = call->get(1)->typeInfo()->getField("addr")->type;

        if(target) {
          INT_ASSERT(valueType == target->typeInfo());
        }

        ret = codegenDeref(call->get(1));

      } else if (target && target->typeInfo()->symbol->hasFlag(FLAG_STAR_TUPLE)) {
        // star tuple retval in codegenAssign
        ret = codegenDeref(call->get(1));

      } else {
        ret = codegenDeref(call->get(1));
      }

      retval = true;
      break;
    }

    case PRIM_GET_MEMBER_VALUE: {
      GenRet obj = call->get(1);
      SymExpr* se = toSymExpr(call->get(2));

      if (target && call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        if (se->symbol()->hasFlag(FLAG_SUPER_CLASS)) {
          // We're getting the super class pointer.
          Type*  addrType = target->typeInfo()->getField("addr")->type;
          GenRet addr     = codegenCast(addrType, codegenRaddr(obj));
          GenRet ref      = codegenWideAddrWithAddr(obj, addr);

          ret = ref;
        } else {
          ret = codegenFieldPtr(obj, se);
        }

      } else if (call->get(1)->isWideRef()) {
        ret = codegenFieldPtr(obj, se);

      } else if (call->get(2)->typeInfo()->symbol->hasFlag(FLAG_STAR_TUPLE)) {
        ret = codegenFieldPtr(obj, se);

      } else if (se->symbol()->hasFlag(FLAG_SUPER_CLASS)) {
        // We're getting the super class pointer.
        GenRet ref = codegenFieldPtr(obj, se);

        // Now we have a field pointer to object->super, but
        // the pointer to super *is* actually the value of
        // the super class. So we just set isPtr to Value.
        ref.isLVPtr = GEN_VAL;

        ret = ref;

      } else {
        ret = codegenFieldPtr(obj, se);
      }
      if (isHeapAllocatedType(call->get(1)->getValType()))
        ret.mustPointOutsideOrderIndependentLoop = true;
      else
        ret.mustPointOutsideOrderIndependentLoop =
          obj.mustPointOutsideOrderIndependentLoop;

      retval = true;
      break;
    }

    case PRIM_GET_MEMBER: {
      /* Get a pointer to a member */
      GenRet obj = call->get(1);
      SymExpr* se = toSymExpr(call->get(2));

      // Invalid AST to use PRIM_GET_MEMBER with a ref field
      INT_ASSERT(!call->get(2)->isRefOrWideRef());

      if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) ||
          call->get(1)->isWideRef()   ||
          call->typeInfo()->symbol->hasFlag(FLAG_STAR_TUPLE)) {

        ret = codegenAddrOf(codegenFieldPtr(obj, se));

        retval = true;
      } else if (target && ((target->isRef() && call->get(2)->isRef()) ||
                 (target->isWideRef() && call->get(2)->isWideRef()))) {
        ret = codegenFieldPtr(obj, se);
        retval = true;

      } else if (target && (target->getValType() != call->get(2)->typeInfo())) {
        // get a narrow reference to the actual 'addr' field
        // of the wide pointer
        GenRet getField = codegenFieldPtr(obj, se);

        ret = codegenAddrOf(codegenWideThingField(getField, WIDE_GEP_ADDR));

        retval = true;
      }
      if (isHeapAllocatedType(call->get(1)->getValType()))
        ret.mustPointOutsideOrderIndependentLoop = true;
      else
        ret.mustPointOutsideOrderIndependentLoop =
          obj.mustPointOutsideOrderIndependentLoop;

      break;
    }

    case PRIM_GET_SVEC_MEMBER: {
      GenRet tup = call->get(1);

      if (call->get(1)->isWideRef()) {
        /* Get a pointer to the i'th element of a homogeneous tuple */
        GenRet elemPtr = codegenElementPtr(tup, call->get(2));

        INT_ASSERT( elemPtr.isLVPtr == GEN_WIDE_PTR );

        elemPtr = codegenAddrOf(elemPtr);

        ret = elemPtr;

        retval = true;

      } else if (target && (target->getValType() != call->getValType())) {
        GenRet getElem = codegenElementPtr(tup, call->get(2));


        ret =  codegenAddrOf(codegenWideThingField(getElem, WIDE_GEP_ADDR));

        retval = true;
      }
      ret.mustPointOutsideOrderIndependentLoop =
        tup.mustPointOutsideOrderIndependentLoop;

      break;
    }

    case PRIM_GET_SVEC_MEMBER_VALUE: {
      /* Get the i'th value from a homogeneous tuple */

      GenRet tup = call->get(1);
      ret =  codegenElementPtr(tup, call->get(2));

      retval = true;
      break;
    }

    case PRIM_ARRAY_GET: {
      /* Get a pointer to the i'th array element */
      // ('_array_get' array idx)
      GenRet aPtr = call->get(1);
      GenRet elem = codegenElementPtr(aPtr, call->get(2));
      GenRet ref  = codegenAddrOf(elem);

      TypeSymbol* arrTS = call->get(1)->typeInfo()->symbol;

      // Handle the case that the array is a wide class/reference
      if (arrTS->hasFlag(FLAG_WIDE_CLASS) || arrTS->hasFlag(FLAG_WIDE_REF)) {
        ret = ref;

      // array is not wide, but what if the target is?
      } else if (target &&
                 (target->qualType().isWideRef() ||
                  target->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))) {
        // resulting reference is wide, but the array is local.
        // This can happen with c_ptr for extern integration...
        ret =  codegenAddrOf(codegenWideHere(ref));

      } else {
        ret = ref;
      }
      if (isHeapAllocatedType(arrTS->getValType()))
        ret.mustPointOutsideOrderIndependentLoop = true;
      else
        ret.mustPointOutsideOrderIndependentLoop =
          aPtr.mustPointOutsideOrderIndependentLoop;

      retval = true;
      break;
    }

    case PRIM_GET_UNION_ID: {
      if (call->get(1)->isWideRef()) {
        ret = codegenFieldUidPtr(call->get(1));
        retval = true;
      }

      break;
    }

    case PRIM_TESTCID: {
      // set get(1) to
      //   call->get(1)->chpl_class_id == chpl__cid_"rhs->get(2)"
      if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        GenRet tmp = codegenFieldCidPtr(call->get(1));
        GenRet cid = codegenUseCid(call->get(2)->typeInfo());

        ret = codegenEquals(tmp, cid);
        retval = true;
      }

      break;
    }

    case PRIM_GETCID: {
      if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        GenRet tmp = codegenFieldCidPtr(call->get(1));

        ret = tmp;
        retval = true;
      }

      break;
    }

    case PRIM_CAST: {
      if (call->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        GenRet tmp = call->get(2);
        if (call->get(2)->isRef()) {
          tmp = codegenDeref(tmp);
        }

        ret = codegenWideAddrWithAddr(tmp,
                                      codegenCast(call->get(1)->typeInfo(),
                                                  codegenRaddr(tmp)));

        retval = true;
      } else if (call->isWideRef()) {
        // MPF TODO: Can we remove this case? Why would we cast
        // a ref?

        GenRet tmp = call->get(2);
        // BHARSH TODO:  Should we check if we're casting to a ref?
        if (call->get(2)->isRef()) {
          tmp = codegenDeref(tmp);
        }

        tmp = codegenWideAddrWithAddr(tmp,
                                      codegenCast(call->get(1)->typeInfo(),
                                                  codegenRaddr(tmp)));

        ret = codegenAddrOf(tmp);
        retval = true;
      }
      // Should this handle target being wide?

      break;
    }

    case PRIM_DYNAMIC_CAST: {
      if (call->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        Type*  type         = call->typeInfo()->getField("addr")->type;
        GenRet wideFrom     = codegenValue(call->get(2));
        GenRet wideFromAddr = codegenRaddr(wideFrom);
        GenRet value        = codegenValue(codegenFieldCidPtr(wideFrom));
        GenRet ok           = codegenDynamicCastCheck(value, type);
        GenRet cast         = codegenCast(type, wideFromAddr);
        GenRet nul          = codegenCast(type, codegenNullPointer());
        GenRet addr         = codegenTernary(ok, cast, nul);
        GenRet wide         = codegenWideAddrWithAddr(wideFrom,
                                                      addr,
                                                      call->typeInfo());

        ret = wide;
        retval = true;
      }

      break;
    }

    case PRIM_ON_LOCALE_NUM: {
      // This primitive expects an argument of type chpl_localeID_t.
      INT_ASSERT(call->get(1)->typeInfo() == dtLocaleID);

      ret = call->get(1);

      retval = true;
      break;
    }

    default:
      // OK, we did not handle the RHS as a special case.
      retval = false;
      break;
    }
  }

  return retval;
}

void CallExpr::codegenInvokeOnFun() {
  FnSymbol*           fn       = resolvedFunction();
  GenRet              localeId = get(1);
  const char*         fname    = NULL;
  GenRet              argBundle;
  GenRet              bundleSize;
  std::vector<GenRet> args(6);

  // get(1) is the locale
  // get(2) is a buffer containing bundled arguments
  // get(3) is a the size of the buffer
  // get(4) is a dummy class type for the argument bundle

  if (fn->hasFlag(FLAG_NON_BLOCKING))
    fname = "chpl_executeOnNB";

  else if (fn->hasFlag(FLAG_FAST_ON))
    fname = "chpl_executeOnFast";

  else
    fname = "chpl_executeOn";

  argBundle  = codegenValue(get(2));
  bundleSize = codegenValue(get(3));

  args[0] = codegenValue(localeId);
  args[1] = new_IntSymbol(ftableMap[fn], INT_SIZE_32);
  args[2] = codegenCast("chpl_comm_on_bundle_p", argBundle);
  args[3] = bundleSize;
  args[4] = fn->linenum();
  args[5] = new_IntSymbol(gFilenameLookupCache[fn->fname()], INT_SIZE_32);

  genComment(fn->cname, true);

  codegenCallWithArgs(fname, args);
}

void CallExpr::codegenInvokeTaskFun(const char* name) {
  FnSymbol*           fn            = resolvedFunction();
  GenRet              taskBundle;
  GenRet              bundleSize;

  std::vector<GenRet> args(6);

  // get(1) is a buffer containing bundled arguments
  // get(2) is the buffer's length
  // get(3) is a dummy class type for the argument bundle
  taskBundle   = codegenValue(get(1));
  bundleSize   = codegenValue(get(2));

  // We would like to remove this conditional and always do the true branch,
  // but wanted to limit the impact of this near the release date.
  GenRet outerLocale = codegenCallExpr("chpl_task_getRequestedSubloc");
  args[0] = outerLocale;
  args[1] = new_IntSymbol(ftableMap[fn], INT_SIZE_64);
  args[2] = codegenCast("chpl_task_bundle_p", taskBundle);
  args[3] = bundleSize;
  args[4] = fn->linenum();
  args[5] = new_IntSymbol(gFilenameLookupCache[fn->fname()], INT_SIZE_32);

  genComment(fn->cname, true);

  codegenCallWithArgs(name, args);
}

GenRet CallExpr::codegenBasicPrimitiveExpr() const {
  std::vector<GenRet> args;

  for_alist(actual, argList) {
    GenRet  gen  = actual;
    Symbol* type = actual->typeInfo()->symbol;

    // Make wide pointers/classes local
    if (type->hasFlag(FLAG_WIDE_CLASS) || actual->isWideRef())
      gen = codegenRaddr(gen);

    // Dereference reference or now-local wide reference
    if (actual->isRef() || actual->isWideRef())
      gen = codegenDeref(gen);

    gen = codegenValue(gen);

    args.push_back(gen);
  }

  return codegenCallExprWithArgs(primitive->name, args);
}

/************************************* | **************************************
*                                                                             *
*                                                                             *
************************************** | *************************************/

GenRet ContextCallExpr::codegen() {
  GenRet ret;
  INT_FATAL(this, "ContextCallExpr::codegen called");
  return ret;
}

/************************************ | *************************************
*                                                                           *
*                                                                           *
************************************* | ************************************/

GenRet NamedExpr::codegen() {
  GenRet ret;
  INT_FATAL(this, "NamedExpr::codegen not implemented");
  return ret;
}

/************************************ | *************************************
*                                                                           *
*                                                                           *
************************************* | ************************************/

GenRet IfcConstraint::codegen() {
  INT_FATAL(this, "IfcConstraint::codegen not implemented");
  GenRet dummy;
  return dummy;
}

/************************************ | *************************************
*                                                                           *
*                                                                           *
************************************* | ************************************/

