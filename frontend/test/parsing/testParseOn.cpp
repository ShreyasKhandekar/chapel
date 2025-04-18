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
#include "chpl/uast/AstNode.h"
#include "chpl/uast/Block.h"
#include "chpl/uast/Comment.h"
#include "chpl/uast/Identifier.h"
#include "chpl/uast/Module.h"
#include "chpl/uast/On.h"

static void test0(Parser* parser) {
  ErrorGuard guard(parser->context());
  auto parseResult = parseStringAndReportErrors(parser, "test0.chpl",
      "/* comment 1 */\n"
      "on /* comment 2 */ foo /* comment 3 */ do\n"
      "  var a;\n"
      "/* comment 4 */\n");
  assert(!guard.realizeErrors());
  auto mod = parseResult.singleModule();
  assert(mod);
  assert(mod->numStmts() == 3);
  assert(mod->stmt(0)->isComment());
  assert(mod->stmt(1)->isOn());
  assert(mod->stmt(2)->isComment());
  const On* on = mod->stmt(1)->toOn();
  assert(on != nullptr);
  assert(on->destination() != nullptr);
  assert(on->destination()->isIdentifier());
  assert(on->numStmts() == 1);
  assert(on->blockStyle() == BlockStyle::IMPLICIT);
  assert(on->stmt(0)->isVariable());
}

static void test1(Parser* parser) {
  ErrorGuard guard(parser->context());
  auto parseResult = parseStringAndReportErrors(parser, "test1.chpl",
      "/* comment 1 */\n"
      "on /* comment 2 */ foo /* comment 3 */ {\n"
      "  /* comment 4 */\n"
      "  var a;\n"
      "  /* comment 5 */\n"
      "}\n"
      "/* comment 6 */\n");
  assert(!guard.realizeErrors());
  auto mod = parseResult.singleModule();
  assert(mod);
  assert(mod->numStmts() == 3);
  assert(mod->stmt(0)->isComment());
  assert(mod->stmt(1)->isOn());
  assert(mod->stmt(2)->isComment());
  const On* on = mod->stmt(1)->toOn();
  assert(on != nullptr);
  assert(on->destination() != nullptr);
  assert(on->destination()->isIdentifier());
  assert(on->numStmts() == 3);
  assert(on->blockStyle() == BlockStyle::EXPLICIT);
  assert(on->stmt(0)->isComment());
  assert(on->stmt(1)->isVariable());
  assert(on->stmt(2)->isComment());
}

static void test2(Parser* parser) {
  ErrorGuard guard(parser->context());
  auto parseResult = parseStringAndReportErrors(parser, "test2.chpl",
      "/* comment 1 */\n"
      "on foo do { var a; }\n");
  assert(!guard.realizeErrors());
  auto mod = parseResult.singleModule();
  assert(mod);
  assert(mod->numStmts() == 2);
  assert(mod->stmt(0)->isComment());
  assert(mod->stmt(1)->isOn());
  const On* on = mod->stmt(1)->toOn();
  assert(on != nullptr);
  assert(on->destination() != nullptr);
  assert(on->destination()->isIdentifier());
  assert(on->numStmts() == 1);
  assert(on->blockStyle() == BlockStyle::UNNECESSARY_KEYWORD_AND_BLOCK);
  assert(on->stmt(0)->isVariable());
}

int main() {
  Context context;
  Context* ctx = &context;

  auto parser = Parser::createForTopLevelModule(ctx);
  Parser* p = &parser;

  test0(p);
  test1(p);
  test2(p);

  return 0;
}
