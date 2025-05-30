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

#include "DecoratedClassType.h"

#include "AstVisitor.h"
#include "stringutil.h"
#include "symbol.h"
#include "expr.h"
#include "iterator.h"

#include "global-ast-vecs.h"

static const char* nameForUser(const char* className) {
  if (!strcmp(className, "_owned") || !strcmp(className, "_shared"))
    return className+1;

  return className;
}

const char* decoratedTypeAstr(ClassTypeDecoratorEnum d, const char* className) {
  switch (d) {
    case ClassTypeDecorator::BORROWED:
      if (developer)
        return astr("borrowed anynil ", className);
      else
        return astr("borrowed ", className);
    case ClassTypeDecorator::BORROWED_NONNIL:
      return astr("borrowed ", className);
    case ClassTypeDecorator::BORROWED_NILABLE:
      return astr("borrowed ", className, "?");

    case ClassTypeDecorator::UNMANAGED:
      if (developer)
        return astr("unmanaged anynil ", className);
      else
        return astr("unmanaged ", className);
    case ClassTypeDecorator::UNMANAGED_NONNIL:
      return astr("unmanaged ", className);
    case ClassTypeDecorator::UNMANAGED_NILABLE:
      return astr("unmanaged ", className, "?");

    case ClassTypeDecorator::MANAGED:
      if (developer)
        return astr("managed anynil ", className);
      else
        return astr(nameForUser(className));
    case ClassTypeDecorator::MANAGED_NONNIL:
      if (developer)
        return astr("managed ", className);
      else
        return astr(nameForUser(className));
    case ClassTypeDecorator::MANAGED_NILABLE:
      if (developer)
        return astr("managed ", className, "?");
      else
        return astr(nameForUser(className), "?");

    case ClassTypeDecorator::GENERIC:
      if (developer)
        return astr("anymanaged anynil ", className);
      else
        return astr(className);
    case ClassTypeDecorator::GENERIC_NONNIL:
      if (developer)
        return astr("anymanaged ", className);
      else
        return astr(className);
    case ClassTypeDecorator::GENERIC_NILABLE:
      if (developer)
        return astr("anymanaged ", className, "?");
      else
        return astr(className, "?");

    // no default for help from compilation errors
  }
  INT_FATAL("Case not handled");
  return NULL;
}


DecoratedClassType::DecoratedClassType(AggregateType* cls, ClassTypeDecoratorEnum d)
  : Type(E_DecoratedClassType, NULL) {

  canonicalClass = cls;
  decorator = d;
  gDecoratedClassTypes.add(this);
}

void DecoratedClassType::accept(AstVisitor* visitor) {
  if (visitor->enterDecoratedClassType(this)) {
    visitor->exitDecoratedClassType(this);
  }
}

void DecoratedClassType::replaceChild(BaseAST* oldAst, BaseAST* newAst) {
  if (oldAst == canonicalClass) {
    canonicalClass = toAggregateType(newAst);
  }
}

void DecoratedClassType::verify() {
  INT_ASSERT(canonicalClass);
  INT_ASSERT(canonicalClass->getDecoratedClass(decorator) == this);
}

GenRet DecoratedClassType::codegen() {
  INT_FATAL("DecoratedClassType should not exist by codegen");
  GenRet ret;
  return ret;
}

DecoratedClassType* DecoratedClassType::copyInner(SymbolMap* map) {
  DecoratedClassType* copy = new DecoratedClassType(canonicalClass, decorator);
  return copy;
}

/*

 A plain class name represents a borrow. The borrow forms the canonical class
 representation, which is used for most purposes within the compiler.

 */
AggregateType* DecoratedClassType::getCanonicalClass() const {
  INT_ASSERT(this->canonicalClass);
  return this->canonicalClass;
}

bool classesWithSameKind(Type* a, Type* b) {
  if (!a || !b) return false;

  // TODO: handle managed pointers
  if (!isClassLike(a) || !isClassLike(b)) return false;


  // AggregateType would mean nonnil borrow
  ClassTypeDecoratorEnum aDecorator = classTypeDecorator(a);
  ClassTypeDecoratorEnum bDecorator = classTypeDecorator(b);

  return aDecorator == bDecorator;
}

// Returns the AggregateType referred to be a DecoratedClassType
// and leaves other types (e.g. owned(SomeClass) unmodified).
Type* canonicalDecoratedClassType(Type* t) {
  if (AggregateType* at = toAggregateType(t))
    if (isClass(at))
        return at;

  if (DecoratedClassType* mt = toDecoratedClassType(t))
    return mt->getCanonicalClass();

  return t;
}

// As with canonicalDecoratedClassType but also handles dtBorrowedNilable etc
// and for managed types like owned SomeClass?, returns SomeClass.
Type* canonicalClassType(Type* t) {

  if (t == dtBorrowed ||
      t == dtBorrowedNonNilable ||
      t == dtBorrowedNilable ||
      t == dtUnmanaged ||
      t == dtUnmanagedNonNilable ||
      t == dtUnmanagedNilable)
    return dtBorrowed;

  if (isManagedPtrType(t)) {
    Type* b = getManagedPtrBorrowType(t);
    if (b && b != dtUnknown)
      return canonicalDecoratedClassType(b);
  }

  return canonicalDecoratedClassType(t);
}

/* If 't' is a class like type or a managed type, this returns
   the DecoratedClassType or AggregateType that represents
   overriding the decorator in 't' (if any) with 'd'.

   (A) When 't' is or wraps some MyClass, the result is:

    (a1) owned or shared, non-nilable:
           // owned non-nilable MyClass
           _owned(MyClass)

    (a2) owned or shared, nilable or generic nilability:
           // owned nilable MyClass
           _owned(DecoratedClassType(ClassTypeDecorator::BORROWED_NILABLE, MyClass))

    (a3) borrowed or unmanaged or generic management, any nilability:
           // unmanaged, generic nilability
           DecoratedClassType(ClassTypeDecorator::UNMANAGED, MyClass)
           // generic management, non-nilable
           DecoratedClassType(ClassTypeDecorator::GENERIC_NONNIL, MyClass)

    (a4) ... except the canonical type:
           // borrowed, non-nilable
           MyClass

    where 'MyClass' denotes its AggregateType.

   (B) When 't' is generic w.r.t. the underlying class, the result is:

    (b1) owned or shared, any nilability:
           // owned, nilable
           DecoratedClassType(ClassTypeDecorator::MANAGED_NILABLE, dtOwned)

    (b2) borrowed or unmanaged or generic management, any nilability:
           // borrowed, generic nilability
           dtBorrowed
           // unmanaged, non-nilable
           dtUnmanagedNonNilable
           // generic management, generic nilability
           dtAnyManagementAnyNilable

   This function will transform generic class types, e.g.
     dtUnmanagedNonNilable + BORROWED_NILABLE -> dtBorrowedNilable

   Note that for owned/shared types, they represent the decorator in two
   ways, depending on whether or not the contained class is specified.

   A plain AggregateType represents a non-nilable borrowed class.

 */
Type* getDecoratedClass(Type* t, ClassTypeDecoratorEnum d) {

  // no _ddata c_ptr etc
  INT_ASSERT(isClassLikeOrManaged(t));

  if (DecoratedClassType* dt = toDecoratedClassType(t)) {
    return dt->getCanonicalClass()->getDecoratedClass(d);
  } else if (isClassLike(t) && isClass(t)) {
    AggregateType* at = toAggregateType(t);
    return at->getDecoratedClass(d);
  } else if (isManagedPtrType(t)) {
    if (d != ClassTypeDecorator::MANAGED &&
        d != ClassTypeDecorator::MANAGED_NONNIL &&
        d != ClassTypeDecorator::MANAGED_NILABLE) {
      Type* bt = getManagedPtrBorrowType(t);
      if (bt && bt != dtUnknown) {
        AggregateType* a = toAggregateType(canonicalClassType(bt));
        INT_ASSERT(a);
        return a->getDecoratedClass(d);
      }
    }

    // Otherwise, use the generic owned/shared with the appropriate decorator
    AggregateType* a = toAggregateType(t);
    INT_ASSERT(a);
    while (a->instantiatedFrom)
      a = a->instantiatedFrom;
    INT_ASSERT(a && isManagedPtrType(a));

    return a->getDecoratedClass(d);
  }

  // Otherwise, it is e.g. generic dtOwned / generic dtBorrowed
  switch (d) {
    case ClassTypeDecorator::BORROWED:
      return dtBorrowed;
    case ClassTypeDecorator::BORROWED_NONNIL:
      return dtBorrowedNonNilable;
    case ClassTypeDecorator::BORROWED_NILABLE:
      return dtBorrowedNilable;
    case ClassTypeDecorator::UNMANAGED:
      return dtUnmanaged;
    case ClassTypeDecorator::UNMANAGED_NONNIL:
      return dtUnmanagedNonNilable;
    case ClassTypeDecorator::UNMANAGED_NILABLE:
      return dtUnmanagedNilable;
    case ClassTypeDecorator::MANAGED:
    case ClassTypeDecorator::MANAGED_NONNIL:
    case ClassTypeDecorator::MANAGED_NILABLE:
      INT_FATAL("should be handled above");
    case ClassTypeDecorator::GENERIC:
      return dtAnyManagementAnyNilable;
    case ClassTypeDecorator::GENERIC_NONNIL:
      return dtAnyManagementNonNilable;
    case ClassTypeDecorator::GENERIC_NILABLE:
      return dtAnyManagementNilable;
    // intentionally no default
  }

  return NULL;
}


ClassTypeDecoratorEnum classTypeDecorator(Type* t) {
  if (!isClassLikeOrManaged(t) && !isClassLikeOrPtr(t))
    INT_FATAL("classTypeDecorator called on non-class non-ptr");

  if (isManagedPtrType(t) && !isDecoratedClassType(t)) {
    Type* bt = getManagedPtrBorrowType(t);
    if (bt && bt != dtUnknown) {
      if (isAggregateType(bt)) {
        return ClassTypeDecorator::MANAGED_NONNIL;
      } else if (DecoratedClassType* dt = toDecoratedClassType(bt)) {
        ClassTypeDecoratorEnum dec = dt->getDecorator();
        if (isDecoratorNonNilable(dec))
          return ClassTypeDecorator::MANAGED_NONNIL;
        else if (isDecoratorNilable(dec))
          return ClassTypeDecorator::MANAGED_NILABLE;
        else
          return ClassTypeDecorator::MANAGED;
      }
    } else {
      return ClassTypeDecorator::MANAGED;
    }
  }

  if (isAggregateType(t))
    return ClassTypeDecorator::BORROWED_NONNIL; // default meaning of AggregateType class

  if (DecoratedClassType* dt = toDecoratedClassType(t)) {
    ClassTypeDecoratorEnum d = dt->getDecorator();
    if (d == ClassTypeDecorator::BORROWED)
      return ClassTypeDecorator::BORROWED_NONNIL;
    else if (d == ClassTypeDecorator::UNMANAGED)
      return ClassTypeDecorator::UNMANAGED_NONNIL;
    else
      return d;
  }

  if (t == dtBorrowed)
    return ClassTypeDecorator::BORROWED;
  if (t == dtBorrowedNonNilable)
    return ClassTypeDecorator::BORROWED_NONNIL;
  if (t == dtBorrowedNilable)
    return ClassTypeDecorator::BORROWED_NILABLE;
  if (t == dtUnmanaged)
    return ClassTypeDecorator::UNMANAGED;
  if (t == dtUnmanagedNonNilable)
    return ClassTypeDecorator::UNMANAGED_NONNIL;
  if (t == dtUnmanagedNilable)
    return ClassTypeDecorator::UNMANAGED_NILABLE;
  if (t == dtAnyManagementAnyNilable)
    return ClassTypeDecorator::GENERIC;
  if (t == dtAnyManagementNonNilable)
    return ClassTypeDecorator::GENERIC_NONNIL;
  if (t == dtAnyManagementNilable)
    return ClassTypeDecorator::GENERIC_NILABLE;

  if (t->symbol->hasFlag(FLAG_C_PTR_CLASS) ||
      t->symbol->hasFlag(FLAG_DATA_CLASS) ||
      t == dtStringC ||
      t == dtCFnPtr ||
      t == dtCVoidPtr) {
    return ClassTypeDecorator::UNMANAGED_NILABLE;
  }

  INT_FATAL("case not handled");
  return ClassTypeDecorator::BORROWED;
}

bool isNonNilableClassType(Type* t) {
  if (!isClassLike(t) && !isManagedPtrType(t))
    return false;

  ClassTypeDecoratorEnum decorator = classTypeDecorator(t);
  return isDecoratorNonNilable(decorator) ||
         decorator == ClassTypeDecorator::BORROWED ||
         decorator == ClassTypeDecorator::UNMANAGED;
}

bool isNilableClassType(Type* t) {
  if (!isClassLike(t) && !isManagedPtrType(t))
    return false;

  ClassTypeDecoratorEnum decorator = classTypeDecorator(t);
  return isDecoratorNilable(decorator);
}

static Type* convertToCanonical(Type* a) {

  if (isReferenceType(a)) {
    // Convert ref(unmanaged) to ref(canonical)
    Type* elt = a->getValType();
    Type* newElt = canonicalDecoratedClassType(elt);
    INT_ASSERT(newElt->refType);
    return newElt->refType;
  }

  // convert unmanaged to canonical
  return canonicalDecoratedClassType(a);
}

static void convertClassTypes(void) {
  AdjustTypeFn adjustTypeFn = convertToCanonical;
  adjustAllSymbolTypes(adjustTypeFn);
}

void convertClassTypesToCanonical() {
  USR_STOP();

  // Anything that has unmanaged pointer type should be using the canonical
  // type instead.
  convertClassTypes();

  // At this point the TypeSymbols for the unmanaged types should
  // be removed from the tree. Using these would be an error.
  forv_Vec(TypeSymbol, ts, gTypeSymbols) {
    if (isDecoratedClassType(ts->type)) {
      if (ts->inTree())
        ts->defPoint->remove();
      if (ts->type->refType && ts->type->refType->symbol->inTree())
        ts->type->refType->symbol->defPoint->remove();
    }
  }

  // Remove useless casts
  forv_Vec(CallExpr, move, gCallExprs) {
    if (move->inTree()) {
      if (move->isPrimitive(PRIM_MOVE) ||
          move->isPrimitive(PRIM_ASSIGN)) {
        SymExpr* lhsSe = toSymExpr(move->get(1));
        CallExpr* rhsCast = toCallExpr(move->get(2));
        if (rhsCast && rhsCast->isPrimitive(PRIM_CAST)) {
          SymExpr* rhsSe = toSymExpr(rhsCast->get(2));
          if (lhsSe->typeInfo() == rhsSe->typeInfo()) {
            rhsSe->remove();
            rhsCast->replace(rhsSe);
          }
        }
      }
    }
  }
}

bool isClassDecoratorPrimitive(CallExpr* call) {
  return (call->isPrimitive(PRIM_TO_UNMANAGED_CLASS) ||
          call->isPrimitive(PRIM_TO_UNMANAGED_CLASS_CHECKED) ||
          call->isPrimitive(PRIM_TO_BORROWED_CLASS) ||
          call->isPrimitive(PRIM_TO_BORROWED_CLASS_CHECKED) ||
          call->isPrimitive(PRIM_TO_NILABLE_CLASS) ||
          call->isPrimitive(PRIM_TO_NILABLE_CLASS_CHECKED) ||
          call->isPrimitive(PRIM_TO_NON_NILABLE_CLASS));
}
