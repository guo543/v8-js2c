#include "src/js2c/c-code-generator.h"

#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "src/ast/ast-value-factory.h"
#include "src/ast/scopes.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/common/globals.h"
#include "src/objects/objects-inl.h"
#include "src/regexp/regexp-flags.h"
#include "src/strings/string-builder-inl.h"

namespace v8 {
namespace internal {

void CCodeGenerator::Init() {
  if (size_ == 0) {
    DCHECK_NULL(output_);
    const int initial_size = 256;
    output_ = NewArray<char>(initial_size);
    size_ = initial_size;
  }
  output_[0] = '\0';
  pos_ = 0;
}

void CCodeGenerator::Print(const char* format, ...) {
  for (;;) {
    va_list arguments;
    va_start(arguments, format);
    int n = base::VSNPrintF(base::Vector<char>(output_, size_) + pos_, format,
                            arguments);
    va_end(arguments);

    if (n >= 0) {
      // there was enough space - we are done
      pos_ += n;
      return;
    } else {
      // there was not enough space - allocate more and try again
      const int slack = 32;
      int new_size = size_ + (size_ >> 1) + slack;
      char* new_output = NewArray<char>(new_size);
      MemCopy(new_output, output_, pos_);
      DeleteArray(output_);
      output_ = new_output;
      size_ = new_size;
    }
  }
}

void CCodeGenerator::PrintLiteral(Literal* literal, bool quote) {
  switch (literal->type()) {
    case Literal::kString:
      PrintLiteral(literal->AsRawString(), quote);
      break;
    case Literal::kSmi:
      Print("%d", Smi::ToInt(literal->AsSmiLiteral()));
      break;
    case Literal::kHeapNumber:
      Print("%g", literal->AsNumber());
      break;
    case Literal::kBigInt:
      Print("%sn", literal->AsBigInt().c_str());
      break;
    case Literal::kNull:
      Print("null");
      break;
    case Literal::kUndefined:
      Print("undefined");
      break;
    case Literal::kTheHole:
      Print("the hole");
      break;
    case Literal::kBoolean:
      if (literal->ToBooleanIsTrue()) {
        Print("true");
      } else {
        Print("false");
      }
      break;
  }
}

void CCodeGenerator::PrintLiteral(const AstRawString* value, bool quote) {
  if (quote) Print("\"");
  if (value != nullptr) {
    const char* format = value->is_one_byte() ? "%c" : "%lc";
    const int increment = value->is_one_byte() ? 1 : 2;
    const unsigned char* raw_bytes = value->raw_data();
    for (int i = 0; i < value->length(); i += increment) {
      if (raw_bytes[i] == '.') {
        Print(format, '_');
      } else {
        Print(format, raw_bytes[i]);
      }
    }
  }
  if (quote) Print("\"");
}

void CCodeGenerator::PrintLiteral(const AstConsString* value, bool quote) {
  if (quote) Print("\"");
  if (value != nullptr) {
    std::forward_list<const AstRawString*> strings = value->ToRawStrings();
    for (const AstRawString* string : strings) {
      PrintLiteral(string, false);
    }
  }
  if (quote) Print("\"");
}

//-----------------------------------------------------------------------------

class V8_NODISCARD CIndentedScope {
 public:
  CIndentedScope(CCodeGenerator* printer, const char* txt)
      : c_code_generator_(printer) {
    c_code_generator_->PrintIndented(txt);
    c_code_generator_->Print("\n");
    c_code_generator_->inc_indent();
  }

  CIndentedScope(CCodeGenerator* printer, const char* txt, int pos)
      : c_code_generator_(printer) {
    c_code_generator_->PrintIndented(txt);
    c_code_generator_->Print(" at %d\n", pos);
    c_code_generator_->inc_indent();
  }

  virtual ~CIndentedScope() {
    c_code_generator_->dec_indent();
  }

 private:
  CCodeGenerator* c_code_generator_;
};

//-----------------------------------------------------------------------------

CCodeGenerator::CCodeGenerator(uintptr_t stack_limit)
    : output_(nullptr), size_(0), pos_(0), indent_(0) {
  InitializeAstVisitor(stack_limit);
  // c_file_fd_ = open("test.c",
  //     O_TRUNC | O_CREAT | O_WRONLY,
  //     S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
}

CCodeGenerator::~CCodeGenerator() {
  DCHECK_EQ(indent_, 0);
  DeleteArray(output_);
  Print("int main() { return _js_entry(); }");
  // close(c_file_fd_);
}


void CCodeGenerator::PrintIndented(const char* txt) {
  for (int i = 0; i < indent_; i++) {
    Print("  ");
  }
  Print("%s", txt);
}

void CCodeGenerator::PrintLiteralIndented(const char* info, Literal* literal,
                                      bool quote) {
  PrintIndented(info);
  Print(" ");
  PrintLiteral(literal, quote);
  Print("\n");
}

void CCodeGenerator::PrintLiteralIndented(const char* info,
                                      const AstRawString* value, bool quote) {
  PrintIndented(info);
  Print(" ");
  PrintLiteral(value, quote);
  Print("\n");
}

void CCodeGenerator::PrintLiteralIndented(const char* info,
                                      const AstConsString* value, bool quote) {
  PrintIndented(info);
  Print(" ");
  PrintLiteral(value, quote);
  Print("\n");
}

void CCodeGenerator::PrintLiteralWithModeIndented(const char* info, Variable* var,
                                              const AstRawString* value) {
  if (var == nullptr) {
    PrintLiteralIndented(info, value, true);
  } else {
    base::EmbeddedVector<char, 256> buf;
    int pos =
        SNPrintF(buf, "%s (%p) (mode = %s, assigned = %s", info,
                 reinterpret_cast<void*>(var), VariableMode2String(var->mode()),
                 var->maybe_assigned() == kMaybeAssigned ? "true" : "false");
    SNPrintF(buf + pos, ")");
    PrintLiteralIndented(buf.begin(), value, true);
  }
}

void CCodeGenerator::PrintIndentedVisit(const char* s, AstNode* node) {
  if (node != nullptr) {
    CIndentedScope indent(this, s, node->position());
    Visit(node);
  }
}


const char* CCodeGenerator::PrintProgram(FunctionLiteral* program) {
  Init();
  bool empty = program->raw_name()->ToRawStrings().empty();
  if (empty) {
    Print("int _js_entry(");
  } else {
    Print("int ");
    PrintLiteral(program->raw_name(), false);
    Print("(");
  }

  PrintParameters(program->scope());
  Print(") { ");
  if (empty) {
    Print("int _result; ");
  }
  // PrintDeclarations(program->scope()->declarations());
  PrintStatements(program->body());

  Print(" }");

  return output_;
}

const char* CCodeGenerator::PrintFunctionDeclaration(FunctionLiteral* function) {
  Init();
  bool empty = function->raw_name()->ToRawStrings().empty();
  if (empty) {
    Print("int _js_entry(");
  } else {
    Print("int ");
    PrintLiteral(function->raw_name(), false);
    Print("(");
  }

  PrintParameters(function->scope());

  Print(");");
  return output_;
}

const char* CCodeGenerator::Finish() {
  Init();
  Print("int main() { printf(\"%%d\\n\", _js_entry()); return 0; }");
  return output_;
}

void CCodeGenerator::PrintDeclarations(Declaration::List* declarations) {
  if (!declarations->is_empty()) {
    CIndentedScope indent(this, "DECLS");
    for (Declaration* decl : *declarations) Visit(decl);
  }
}

void CCodeGenerator::PrintParameters(DeclarationScope* scope) {
  if (scope->num_parameters() > 0) {
    for (int i = 0; i < scope->num_parameters(); i++) {
      Print("int ");
      PrintLiteral(scope->parameter(i)->raw_name(), false);
      if (i != scope->num_parameters() - 1) {
        Print(", ");
      }
    }
  }
}

void CCodeGenerator::PrintStatements(const ZonePtrList<Statement>* statements) {
  for (int i = 0; i < statements->length(); i++) {
    Visit(statements->at(i));
  }
}

void CCodeGenerator::PrintArguments(const ZonePtrList<Expression>* arguments) {
  for (int i = 0; i < arguments->length(); i++) {
    Visit(arguments->at(i));
    if (i != arguments->length() - 1) {
      Print(", ");
    }
  }
}


void CCodeGenerator::VisitBlock(Block* node) {
  const char* block_txt =
      node->ignore_completion_value() ? "BLOCK NOCOMPLETIONS" : "BLOCK";
  CIndentedScope indent(this, block_txt, node->position());
  PrintStatements(node->statements());
}


// TODO(svenpanne) Start with IndentedScope.
void CCodeGenerator::VisitVariableDeclaration(VariableDeclaration* node) {
  PrintLiteralWithModeIndented("VARIABLE", node->var(),
                               node->var()->raw_name());
}


// TODO(svenpanne) Start with IndentedScope.
void CCodeGenerator::VisitFunctionDeclaration(FunctionDeclaration* node) {
  PrintIndented("FUNCTION ");
  PrintLiteral(node->var()->raw_name(), true);
  Print(" = function ");
  PrintLiteral(node->fun()->raw_name(), false);
  Print("\n");
}


void CCodeGenerator::VisitExpressionStatement(ExpressionStatement* node) {
  // CIndentedScope indent(this, "EXPRESSION STATEMENT", node->position());
  Visit(node->expression());
}


void CCodeGenerator::VisitEmptyStatement(EmptyStatement* node) {
  CIndentedScope indent(this, "EMPTY", node->position());
}


void CCodeGenerator::VisitSloppyBlockFunctionStatement(
    SloppyBlockFunctionStatement* node) {
  Visit(node->statement());
}


void CCodeGenerator::VisitIfStatement(IfStatement* node) {
  CIndentedScope indent(this, "IF", node->position());
  PrintIndentedVisit("CONDITION", node->condition());
  PrintIndentedVisit("THEN", node->then_statement());
  if (node->HasElseStatement()) {
    PrintIndentedVisit("ELSE", node->else_statement());
  }
}


void CCodeGenerator::VisitContinueStatement(ContinueStatement* node) {
  CIndentedScope indent(this, "CONTINUE", node->position());
}


void CCodeGenerator::VisitBreakStatement(BreakStatement* node) {
  CIndentedScope indent(this, "BREAK", node->position());
}


void CCodeGenerator::VisitReturnStatement(ReturnStatement* node) {
  Print("return ");
  Visit(node->expression());
  Print(";");
}


void CCodeGenerator::VisitWithStatement(WithStatement* node) {
  CIndentedScope indent(this, "WITH", node->position());
  PrintIndentedVisit("OBJECT", node->expression());
  PrintIndentedVisit("BODY", node->statement());
}


void CCodeGenerator::VisitSwitchStatement(SwitchStatement* node) {
  CIndentedScope switch_indent(this, "SWITCH", node->position());
  PrintIndentedVisit("TAG", node->tag());
  for (CaseClause* clause : *node->cases()) {
    if (clause->is_default()) {
      CIndentedScope indent(this, "DEFAULT");
      PrintStatements(clause->statements());
    } else {
      CIndentedScope indent(this, "CASE");
      Visit(clause->label());
      PrintStatements(clause->statements());
    }
  }
}


void CCodeGenerator::VisitDoWhileStatement(DoWhileStatement* node) {
  CIndentedScope indent(this, "DO", node->position());
  PrintIndentedVisit("BODY", node->body());
  PrintIndentedVisit("COND", node->cond());
}


void CCodeGenerator::VisitWhileStatement(WhileStatement* node) {
  CIndentedScope indent(this, "WHILE", node->position());
  PrintIndentedVisit("COND", node->cond());
  PrintIndentedVisit("BODY", node->body());
}


void CCodeGenerator::VisitForStatement(ForStatement* node) {
  CIndentedScope indent(this, "FOR", node->position());
  if (node->init()) PrintIndentedVisit("INIT", node->init());
  if (node->cond()) PrintIndentedVisit("COND", node->cond());
  PrintIndentedVisit("BODY", node->body());
  if (node->next()) PrintIndentedVisit("NEXT", node->next());
}


void CCodeGenerator::VisitForInStatement(ForInStatement* node) {
  CIndentedScope indent(this, "FOR IN", node->position());
  PrintIndentedVisit("FOR", node->each());
  PrintIndentedVisit("IN", node->subject());
  PrintIndentedVisit("BODY", node->body());
}


void CCodeGenerator::VisitForOfStatement(ForOfStatement* node) {
  CIndentedScope indent(this, "FOR OF", node->position());
  const char* for_type;
  switch (node->type()) {
    case IteratorType::kNormal:
      for_type = "FOR";
      break;
    case IteratorType::kAsync:
      for_type = "FOR AWAIT";
      break;
  }
  PrintIndentedVisit(for_type, node->each());
  PrintIndentedVisit("OF", node->subject());
  PrintIndentedVisit("BODY", node->body());
}


void CCodeGenerator::VisitTryCatchStatement(TryCatchStatement* node) {
  CIndentedScope indent(this, "TRY CATCH", node->position());
  PrintIndentedVisit("TRY", node->try_block());
  PrintIndented("CATCH PREDICTION");
  const char* prediction = "";
  switch (node->GetCatchPrediction(HandlerTable::UNCAUGHT)) {
    case HandlerTable::UNCAUGHT:
      prediction = "UNCAUGHT";
      break;
    case HandlerTable::CAUGHT:
      prediction = "CAUGHT";
      break;
    case HandlerTable::ASYNC_AWAIT:
      prediction = "ASYNC_AWAIT";
      break;
    case HandlerTable::UNCAUGHT_ASYNC_AWAIT:
      prediction = "UNCAUGHT_ASYNC_AWAIT";
      break;
    case HandlerTable::PROMISE:
      // Catch prediction resulting in promise rejections aren't
      // parsed by the parser.
      UNREACHABLE();
  }
  Print(" %s\n", prediction);
  if (node->scope()) {
    PrintLiteralWithModeIndented("CATCHVAR", node->scope()->catch_variable(),
                                 node->scope()->catch_variable()->raw_name());
  }
  PrintIndentedVisit("CATCH", node->catch_block());
}

void CCodeGenerator::VisitTryFinallyStatement(TryFinallyStatement* node) {
  CIndentedScope indent(this, "TRY FINALLY", node->position());
  PrintIndentedVisit("TRY", node->try_block());
  PrintIndentedVisit("FINALLY", node->finally_block());
}

void CCodeGenerator::VisitDebuggerStatement(DebuggerStatement* node) {
  CIndentedScope indent(this, "DEBUGGER", node->position());
}


void CCodeGenerator::VisitFunctionLiteral(FunctionLiteral* node) {
  CIndentedScope indent(this, "FUNC LITERAL", node->position());
  PrintIndented("LITERAL ID");
  Print(" %d\n", node->function_literal_id());
  PrintLiteralIndented("NAME", node->raw_name(), false);
  PrintLiteralIndented("INFERRED NAME", node->raw_inferred_name(), false);
  // We don't want to see the function literal in this case: it
  // will be printed via PrintProgram when the code for it is
  // generated.
  // PrintParameters(node->scope());
  // PrintStatements(node->body());
}


void CCodeGenerator::VisitClassLiteral(ClassLiteral* node) {
  CIndentedScope indent(this, "CLASS LITERAL", node->position());
  PrintLiteralIndented("NAME", node->constructor()->raw_name(), false);
  if (node->extends() != nullptr) {
    PrintIndentedVisit("EXTENDS", node->extends());
  }
  Scope* outer = node->constructor()->scope()->outer_scope();
  if (outer->is_class_scope()) {
    Variable* brand = outer->AsClassScope()->brand();
    if (brand != nullptr) {
      PrintLiteralWithModeIndented("BRAND", brand, brand->raw_name());
    }
  }
  if (node->static_initializer() != nullptr) {
    PrintIndentedVisit("STATIC INITIALIZER", node->static_initializer());
  }
  if (node->instance_members_initializer_function() != nullptr) {
    PrintIndentedVisit("INSTANCE MEMBERS INITIALIZER",
                       node->instance_members_initializer_function());
  }
  PrintClassProperties(node->private_members());
  PrintClassProperties(node->public_members());
}

void CCodeGenerator::VisitInitializeClassMembersStatement(
    InitializeClassMembersStatement* node) {
  CIndentedScope indent(this, "INITIALIZE CLASS MEMBERS", node->position());
  PrintClassProperties(node->fields());
}

void CCodeGenerator::VisitInitializeClassStaticElementsStatement(
    InitializeClassStaticElementsStatement* node) {
  CIndentedScope indent(this, "INITIALIZE CLASS STATIC ELEMENTS",
                       node->position());
  PrintClassStaticElements(node->elements());
}

void CCodeGenerator::PrintClassProperty(ClassLiteral::Property* property) {
  const char* prop_kind = nullptr;
  switch (property->kind()) {
    case ClassLiteral::Property::METHOD:
      prop_kind = "METHOD";
      break;
    case ClassLiteral::Property::GETTER:
      prop_kind = "GETTER";
      break;
    case ClassLiteral::Property::SETTER:
      prop_kind = "SETTER";
      break;
    case ClassLiteral::Property::FIELD:
      prop_kind = "FIELD";
      break;
  }
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "PROPERTY%s%s - %s", property->is_static() ? " - STATIC" : "",
           property->is_private() ? " - PRIVATE" : " - PUBLIC", prop_kind);
  CIndentedScope prop(this, buf.begin());
  PrintIndentedVisit("KEY", property->key());
  PrintIndentedVisit("VALUE", property->value());
}

void CCodeGenerator::PrintClassProperties(
    const ZonePtrList<ClassLiteral::Property>* properties) {
  for (int i = 0; i < properties->length(); i++) {
    PrintClassProperty(properties->at(i));
  }
}

void CCodeGenerator::PrintClassStaticElements(
    const ZonePtrList<ClassLiteral::StaticElement>* static_elements) {
  for (int i = 0; i < static_elements->length(); i++) {
    ClassLiteral::StaticElement* element = static_elements->at(i);
    switch (element->kind()) {
      case ClassLiteral::StaticElement::PROPERTY:
        PrintClassProperty(element->property());
        break;
      case ClassLiteral::StaticElement::STATIC_BLOCK:
        PrintIndentedVisit("STATIC BLOCK", element->static_block());
        break;
    }
  }
}

void CCodeGenerator::VisitNativeFunctionLiteral(NativeFunctionLiteral* node) {
  CIndentedScope indent(this, "NATIVE FUNC LITERAL", node->position());
  PrintLiteralIndented("NAME", node->raw_name(), false);
}


void CCodeGenerator::VisitConditional(Conditional* node) {
  CIndentedScope indent(this, "CONDITIONAL", node->position());
  PrintIndentedVisit("CONDITION", node->condition());
  PrintIndentedVisit("THEN", node->then_expression());
  PrintIndentedVisit("ELSE", node->else_expression());
}


void CCodeGenerator::VisitLiteral(Literal* node) {
  PrintLiteral(node, false);
}


void CCodeGenerator::VisitRegExpLiteral(RegExpLiteral* node) {
  CIndentedScope indent(this, "REGEXP LITERAL", node->position());
  PrintLiteralIndented("PATTERN", node->raw_pattern(), false);
  int i = 0;
  base::EmbeddedVector<char, 128> buf;
#define V(Lower, Camel, LowerCamel, Char, Bit) \
  if (node->flags() & RegExp::k##Camel) buf[i++] = Char;
  REGEXP_FLAG_LIST(V)
#undef V
  buf[i] = '\0';
  PrintIndented("FLAGS ");
  Print("%s", buf.begin());
  Print("\n");
}


void CCodeGenerator::VisitObjectLiteral(ObjectLiteral* node) {
  CIndentedScope indent(this, "OBJ LITERAL", node->position());
  PrintObjectProperties(node->properties());
}

void CCodeGenerator::PrintObjectProperties(
    const ZonePtrList<ObjectLiteral::Property>* properties) {
  for (int i = 0; i < properties->length(); i++) {
    ObjectLiteral::Property* property = properties->at(i);
    const char* prop_kind = nullptr;
    switch (property->kind()) {
      case ObjectLiteral::Property::CONSTANT:
        prop_kind = "CONSTANT";
        break;
      case ObjectLiteral::Property::COMPUTED:
        prop_kind = "COMPUTED";
        break;
      case ObjectLiteral::Property::MATERIALIZED_LITERAL:
        prop_kind = "MATERIALIZED_LITERAL";
        break;
      case ObjectLiteral::Property::PROTOTYPE:
        prop_kind = "PROTOTYPE";
        break;
      case ObjectLiteral::Property::GETTER:
        prop_kind = "GETTER";
        break;
      case ObjectLiteral::Property::SETTER:
        prop_kind = "SETTER";
        break;
      case ObjectLiteral::Property::SPREAD:
        prop_kind = "SPREAD";
        break;
    }
    base::EmbeddedVector<char, 128> buf;
    SNPrintF(buf, "PROPERTY - %s", prop_kind);
    CIndentedScope prop(this, buf.begin());
    PrintIndentedVisit("KEY", properties->at(i)->key());
    PrintIndentedVisit("VALUE", properties->at(i)->value());
  }
}


void CCodeGenerator::VisitArrayLiteral(ArrayLiteral* node) {
  CIndentedScope array_indent(this, "ARRAY LITERAL", node->position());
  if (node->values()->length() > 0) {
    CIndentedScope indent(this, "VALUES", node->position());
    for (int i = 0; i < node->values()->length(); i++) {
      Visit(node->values()->at(i));
    }
  }
}


void CCodeGenerator::VisitVariableProxy(VariableProxy* node) {
  // base::EmbeddedVector<char, 128> buf;
  // int pos = SNPrintF(buf, "VAR PROXY");

  // if (!node->is_resolved()) {
  //   SNPrintF(buf + pos, " unresolved");
  //   PrintLiteralWithModeIndented(buf.begin(), nullptr, node->raw_name());
  // } else {
  //   Variable* var = node->var();
  //   switch (var->location()) {
  //     case VariableLocation::UNALLOCATED:
  //       SNPrintF(buf + pos, " unallocated");
  //       break;
  //     case VariableLocation::PARAMETER:
  //       SNPrintF(buf + pos, " parameter[%d]", var->index());
  //       break;
  //     case VariableLocation::LOCAL:
  //       SNPrintF(buf + pos, " local[%d]", var->index());
  //       break;
  //     case VariableLocation::CONTEXT:
  //       SNPrintF(buf + pos, " context[%d]", var->index());
  //       break;
  //     case VariableLocation::LOOKUP:
  //       SNPrintF(buf + pos, " lookup");
  //       break;
  //     case VariableLocation::MODULE:
  //       SNPrintF(buf + pos, " module");
  //       break;
  //     case VariableLocation::REPL_GLOBAL:
  //       SNPrintF(buf + pos, " repl global[%d]", var->index());
  //       break;
  //   }
  PrintLiteral(node->raw_name(), false);
}


void CCodeGenerator::VisitAssignment(Assignment* node) {
  // CIndentedScope indent(this, Token::Name(node->op()), node->position());
  Visit(node->target());
  Print(" = ");
  Visit(node->value());
  Print("; ");
}

void CCodeGenerator::VisitCompoundAssignment(CompoundAssignment* node) {
  VisitAssignment(node);
}

void CCodeGenerator::VisitYield(Yield* node) {
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "YIELD");
  CIndentedScope indent(this, buf.begin(), node->position());
  Visit(node->expression());
}

void CCodeGenerator::VisitYieldStar(YieldStar* node) {
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "YIELD_STAR");
  CIndentedScope indent(this, buf.begin(), node->position());
  Visit(node->expression());
}

void CCodeGenerator::VisitAwait(Await* node) {
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "AWAIT");
  CIndentedScope indent(this, buf.begin(), node->position());
  Visit(node->expression());
}

void CCodeGenerator::VisitThrow(Throw* node) {
  CIndentedScope indent(this, "THROW", node->position());
  Visit(node->exception());
}

void CCodeGenerator::VisitOptionalChain(OptionalChain* node) {
  CIndentedScope indent(this, "OPTIONAL_CHAIN", node->position());
  Visit(node->expression());
}

void CCodeGenerator::VisitProperty(Property* node) {
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "PROPERTY");
  CIndentedScope indent(this, buf.begin(), node->position());

  Visit(node->obj());
  AssignType type = Property::GetAssignType(node);
  switch (type) {
    case NAMED_PROPERTY:
    case NAMED_SUPER_PROPERTY: {
      PrintLiteralIndented("NAME", node->key()->AsLiteral(), false);
      break;
    }
    case PRIVATE_METHOD: {
      PrintIndentedVisit("PRIVATE_METHOD", node->key());
      break;
    }
    case PRIVATE_GETTER_ONLY: {
      PrintIndentedVisit("PRIVATE_GETTER_ONLY", node->key());
      break;
    }
    case PRIVATE_SETTER_ONLY: {
      PrintIndentedVisit("PRIVATE_SETTER_ONLY", node->key());
      break;
    }
    case PRIVATE_GETTER_AND_SETTER: {
      PrintIndentedVisit("PRIVATE_GETTER_AND_SETTER", node->key());
      break;
    }
    case KEYED_PROPERTY:
    case KEYED_SUPER_PROPERTY: {
      PrintIndentedVisit("KEY", node->key());
      break;
    }
    case NON_PROPERTY:
      UNREACHABLE();
  }
}

void CCodeGenerator::VisitCall(Call* node) {
  // base::EmbeddedVector<char, 128> buf;
  // SNPrintF(buf, "CALL");
  // CIndentedScope indent(this, buf.begin());

  Visit(node->expression());
  Print("(");
  PrintArguments(node->arguments());
  Print(")");
}


void CCodeGenerator::VisitCallNew(CallNew* node) {
  CIndentedScope indent(this, "CALL NEW", node->position());
  Visit(node->expression());
  PrintArguments(node->arguments());
}


void CCodeGenerator::VisitCallRuntime(CallRuntime* node) {
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "CALL RUNTIME %s%s", node->debug_name(),
           node->is_jsruntime() ? " (JS function)" : "");
  CIndentedScope indent(this, buf.begin(), node->position());
  PrintArguments(node->arguments());
}


void CCodeGenerator::VisitUnaryOperation(UnaryOperation* node) {
  CIndentedScope indent(this, Token::Name(node->op()), node->position());
  Visit(node->expression());
}


void CCodeGenerator::VisitCountOperation(CountOperation* node) {
  base::EmbeddedVector<char, 128> buf;
  SNPrintF(buf, "%s %s", (node->is_prefix() ? "PRE" : "POST"),
           Token::Name(node->op()));
  CIndentedScope indent(this, buf.begin(), node->position());
  Visit(node->expression());
}


void CCodeGenerator::VisitBinaryOperation(BinaryOperation* node) {
//   CIndentedScope indent(this, Token::Name(node->op()), node->position());
  Visit(node->left());
  switch (node->op()) {
  case Token::ADD:
    Print(" + ");
    break;
  case Token::TEMPLATE_SPAN:
  case Token::TEMPLATE_TAIL:
  case Token::PERIOD:
  case Token::LBRACK:
  case Token::QUESTION_PERIOD:
  case Token::LPAREN:
  case Token::RPAREN:
  case Token::RBRACK:
  case Token::LBRACE:
  case Token::COLON:
  case Token::ELLIPSIS:
  case Token::CONDITIONAL:
  case Token::SEMICOLON:
  case Token::RBRACE:
  case Token::EOS:
  case Token::ARROW:
  case Token::INIT:
  case Token::ASSIGN:
  case Token::ASSIGN_NULLISH:
  case Token::ASSIGN_OR:
  case Token::ASSIGN_AND:
  case Token::ASSIGN_BIT_OR:
  case Token::ASSIGN_BIT_XOR:
  case Token::ASSIGN_BIT_AND:
  case Token::ASSIGN_SHL:
  case Token::ASSIGN_SAR:
  case Token::ASSIGN_SHR:
  case Token::ASSIGN_MUL:
  case Token::ASSIGN_DIV:
  case Token::ASSIGN_MOD:
  case Token::ASSIGN_EXP:
  case Token::ASSIGN_ADD:
  case Token::ASSIGN_SUB:
  case Token::COMMA:
  case Token::NULLISH:
  case Token::OR:
  case Token::AND:
  case Token::BIT_OR:
  case Token::BIT_XOR:
  case Token::BIT_AND:
  case Token::SHL:
  case Token::SAR:
  case Token::SHR:
  case Token::MUL:
  case Token::DIV:
  case Token::MOD:
  case Token::EXP:
  case Token::SUB:
    Print(" - ");
    break;
  case Token::NOT:
  case Token::BIT_NOT:
  case Token::DELETE:
  case Token::TYPEOF:
  case Token::VOID:
  case Token::INC:
  case Token::DEC:
  case Token::EQ:
  case Token::EQ_STRICT:
  case Token::NE:
  case Token::NE_STRICT:
  case Token::LT:
  case Token::GT:
  case Token::LTE:
  case Token::GTE:
  case Token::INSTANCEOF:
  case Token::IN:
  case Token::BREAK:
  case Token::CASE:
  case Token::CATCH:
  case Token::CONTINUE:
  case Token::DEBUGGER:
  case Token::DEFAULT:
  case Token::DO:
  case Token::ELSE:
  case Token::FINALLY:
  case Token::FOR:
  case Token::FUNCTION:
  case Token::IF:
  case Token::NEW:
  case Token::RETURN:
  case Token::SWITCH:
  case Token::THROW:
  case Token::TRY:
  case Token::VAR:
  case Token::WHILE:
  case Token::WITH:
  case Token::THIS:
  case Token::NULL_LITERAL:
  case Token::TRUE_LITERAL:
  case Token::FALSE_LITERAL:
  case Token::NUMBER:
  case Token::SMI:
  case Token::BIGINT:
  case Token::STRING:
  case Token::SUPER:
  case Token::IDENTIFIER:
  case Token::GET:
  case Token::SET:
  case Token::ASYNC:
  case Token::AWAIT:
  case Token::YIELD:
  case Token::LET:
  case Token::STATIC:
  case Token::FUTURE_STRICT_RESERVED_WORD:
  case Token::ESCAPED_STRICT_RESERVED_WORD:
  case Token::ENUM:
  case Token::CLASS:
  case Token::CONST:
  case Token::EXPORT:
  case Token::EXTENDS:
  case Token::IMPORT:
  case Token::PRIVATE_NAME:
  case Token::ILLEGAL:
  case Token::ESCAPED_KEYWORD:
  case Token::WHITESPACE:
  case Token::UNINITIALIZED:
  case Token::REGEXP_LITERAL:
  case Token::NUM_TOKENS:
    break;
  }
  Visit(node->right());
}

void CCodeGenerator::VisitNaryOperation(NaryOperation* node) {
  // CIndentedScope indent(this, Token::Name(node->op()), node->position());
  Visit(node->first());
  Print(" %s ", Token::String(node->op()));
  for (size_t i = 0; i < node->subsequent_length(); ++i) {
    Visit(node->subsequent(i));

    if (i != node->subsequent_length() - 1) {
      Print(" %s ", Token::String(node->op()));
    }
  }
}

void CCodeGenerator::VisitCompareOperation(CompareOperation* node) {
  CIndentedScope indent(this, Token::Name(node->op()), node->position());
  Visit(node->left());
  Visit(node->right());
}


void CCodeGenerator::VisitSpread(Spread* node) {
  CIndentedScope indent(this, "SPREAD", node->position());
  Visit(node->expression());
}

void CCodeGenerator::VisitEmptyParentheses(EmptyParentheses* node) {
  CIndentedScope indent(this, "()", node->position());
}

void CCodeGenerator::VisitGetTemplateObject(GetTemplateObject* node) {
  CIndentedScope indent(this, "GET-TEMPLATE-OBJECT", node->position());
}

void CCodeGenerator::VisitTemplateLiteral(TemplateLiteral* node) {
  CIndentedScope indent(this, "TEMPLATE-LITERAL", node->position());
  const AstRawString* string = node->string_parts()->first();
  if (!string->IsEmpty()) PrintLiteralIndented("SPAN", string, true);
  for (int i = 0; i < node->substitutions()->length();) {
    PrintIndentedVisit("EXPR", node->substitutions()->at(i++));
    if (i < node->string_parts()->length()) {
      string = node->string_parts()->at(i);
      if (!string->IsEmpty()) PrintLiteralIndented("SPAN", string, true);
    }
  }
}

void CCodeGenerator::VisitImportCallExpression(ImportCallExpression* node) {
  CIndentedScope indent(this, "IMPORT-CALL", node->position());
  Visit(node->specifier());
  if (node->import_assertions()) {
    Visit(node->import_assertions());
  }
}

void CCodeGenerator::VisitThisExpression(ThisExpression* node) {
  CIndentedScope indent(this, "THIS-EXPRESSION", node->position());
}

void CCodeGenerator::VisitSuperPropertyReference(SuperPropertyReference* node) {
  CIndentedScope indent(this, "SUPER-PROPERTY-REFERENCE", node->position());
}


void CCodeGenerator::VisitSuperCallReference(SuperCallReference* node) {
  CIndentedScope indent(this, "SUPER-CALL-REFERENCE", node->position());
}

}  // namespace internal
}  // namespace v8
