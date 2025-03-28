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

#include "test-parsing.h"

#include "chpl/parsing/Parser.h"
#include "chpl/framework/Context.h"
#include "chpl/framework/ErrorBase.h"
#include "chpl/uast/AstNode.h"
#include "chpl/uast/Block.h"
#include "chpl/uast/Identifier.h"
#include "chpl/uast/Module.h"
#include "chpl/uast/Select.h"
#include "chpl/uast/When.h"

#include <iostream>

static void test0(Parser* parser) {
  printf("test0\n");
  ErrorGuard guard(parser->context());
  auto parseResult = parseStringAndReportErrors(parser, "test0.chpl",
      "/* c1 */\n"
      "select /* c2 */ foo /* c3 */ {\n"
      "  when /* c4 */ x do /* c5 */ f1();\n"
      "  when /* c6 */ y /* c7 */ { f2(); }\n"
      "  when /* c8 */ z /* c9 */ do { f3(); }\n"
      "  when a, b, c do f4();\n"
      "  otherwise /* c10 */ { f5(); }\n"
      "}\n"
      "/* c11 */\n");
  guard.printErrors();
  assert(!guard.realizeErrors());
  auto mod = parseResult.singleModule();
  assert(mod);
  assert(mod->numStmts() == 3);
  assert(mod->stmt(0)->isComment());
  assert(mod->stmt(1)->isSelect());
  assert(mod->stmt(2)->isComment());
  auto select = mod->stmt(1)->toSelect();

  assert(select->expr()->isIdentifier());

  // Verify the number of when statements.
  int idx = 0;
  for (auto when : select->whenStmts()) {
    assert(when == select->whenStmt(idx++));
    assert(when->isWhen());
  }

  assert(idx == select->numWhenStmts());
  assert(select->numWhenStmts() == 5);

  for (int i = 0; i < select->numWhenStmts(); i++) {
    auto when = select->whenStmt(i);

    // Verify the block style.
    auto bs = when->blockStyle();
    if (i == 0) assert(bs == BlockStyle::IMPLICIT);
    if (i == 1) assert(bs == BlockStyle::EXPLICIT);
    if (i == 2) assert(bs == BlockStyle::UNNECESSARY_KEYWORD_AND_BLOCK);
    if (i == 3) assert(bs == BlockStyle::IMPLICIT);
    if (i == 4) assert(bs == BlockStyle::EXPLICIT);

    // Count the number of case expressions.
    int numCaseExprs = 0;
    for (auto caseExpr : when->caseExprs()) {
      assert(caseExpr->isIdentifier());
      assert(caseExpr == when->caseExpr(numCaseExprs++));
    }

    assert(numCaseExprs == when->numCaseExprs());

    if (i == 4) {
      assert(when->numCaseExprs() == 0);
    } else if (i == 3) {
      assert(when->numCaseExprs() == 3);
    } else {
      assert(when->numCaseExprs() == 1);
    }

    if (i == 4) {
      assert(when->isOtherwise());
    } else {
      assert(!when->isOtherwise());
    }

    assert(when->body()->numStmts() == 1);
    assert(when->body()->stmt(0)->isFnCall());
  }
}

// Should be parse error.
static void test1(Parser* parser) {
  printf("test1\n");
  ErrorGuard guard(parser->context());
  auto parseResult = parseStringAndReportErrors(parser, "test1.chpl",
      "/* c1 */\n"
      "select foo {\n"
      "  when x do f1();\n"
      "  otherwise do f2();\n"
      "  otherwise { f3(); }\n"
      "  otherwise do { f4(); }\n"
      "}\n"
      "/* c2 */\n");
  guard.printErrors();
  assert(guard.numErrors() == 1);
  auto mod = parseResult.singleModule();
  assert(mod);
  assert(mod->stmt(0)->isComment());
  assert(mod->stmt(1)->isErroneousExpression());
  assert(mod->stmt(2)->isComment());
  auto& error = guard.error(0);
  const char* expected = "select has multiple otherwise clauses";
  auto actual = error->message();
  assert(actual == expected);
  guard.clearErrors();
}

static void test2(Parser* parser) {
  printf("test2\n");
  ErrorGuard guard(parser->context());
  auto parseResult = parseStringAndReportErrors(parser, "test2.chpl",
      "/* c1 */\n"
      "select foo {\n"
      "  when x, y do f1();\n"
      "  otherwise do f2();\n"
      "}\n"
      "/* c2 */\n");
  guard.printErrors();
  assert(!guard.realizeErrors());
  auto mod = parseResult.singleModule();
  assert(mod);
  assert(mod->numStmts() == 3);
  assert(mod->stmt(0)->isComment());
  assert(mod->stmt(1)->isSelect());
  assert(mod->stmt(2)->isComment());
  auto select = mod->stmt(1)->toSelect();
  assert(select->numWhenStmts() == 2);
  auto w0 = select->whenStmt(0);
  assert(!w0->isOtherwise());
  assert(w0->numCaseExprs() == 2);
  assert(w0->blockStyle() == BlockStyle::IMPLICIT);
  auto w1 = select->whenStmt(1);
  assert(w1->isOtherwise());
  assert(w1->numCaseExprs() == 0);
  assert(w1->blockStyle() == BlockStyle::IMPLICIT);
}

static void test3(Parser* parser) {
  printf("test3\n");
  ErrorGuard guard(parser->context());
  auto parseResult = parseStringAndReportErrors(parser, "test3.chpl",
      "/* c1 */\n"
      "select foo {\n"
      "  otherwise do { f1(); }\n"
      "}\n"
      "/* c2 */\n");
  guard.printErrors();
  assert(!guard.realizeErrors());
  auto mod = parseResult.singleModule();
  assert(mod);
  assert(mod->numStmts() == 3);
  assert(mod->stmt(0)->isComment());
  assert(mod->stmt(1)->isSelect());
  assert(mod->stmt(2)->isComment());
  auto select = mod->stmt(1)->toSelect();
  assert(select->numWhenStmts() == 1);
  auto w0 = select->whenStmt(0);
  assert(w0->isOtherwise());
  assert(w0->numCaseExprs() == 0);
  assert(w0->blockStyle() == BlockStyle::UNNECESSARY_KEYWORD_AND_BLOCK);
  assert(w0->body()->numStmts() == 1);
}

static void test4(Parser* parser) {
  printf("test4\n");
  ErrorGuard guard(parser->context());
  auto parseResult = parseStringAndReportErrors(parser, "test4.chpl",
      "/* c1 */\n"
      "select foo {\n"
      "  when x do f1();\n"
      "  otherwise do f2();\n"
      "  when y { f3(); }\n"
      "}\n"
      "/* c2 */\n");
  guard.printErrors();
  assert(guard.numErrors() == 1);
  auto mod = parseResult.singleModule();
  assert(mod);
  assert(mod->stmt(0)->isComment());
  assert(mod->stmt(1)->isSelect());
  assert(mod->stmt(2)->isComment());
  auto& error = guard.error(0);
  assert(error->type() == ErrorType::WhenAfterOtherwise);
  guard.clearErrors();
}

int main() {
  Context context;
  Context* ctx = &context;

  auto parser = Parser::createForTopLevelModule(ctx);
  Parser* p = &parser;

  test0(p);
  test1(p);
  test2(p);
  test3(p);
  test4(p);
  return 0;
}
