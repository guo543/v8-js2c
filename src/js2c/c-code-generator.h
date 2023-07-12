#ifndef V8_C_CODE_GENERATOR_H_
#define V8_C_CODE_GENERATOR_H_

#include <memory>

#include "src/ast/ast.h"
#include "src/base/compiler-specific.h"
#include "src/execution/isolate.h"
#include "src/objects/function-kind.h"

namespace v8 {
namespace internal {

class CCodeGenerator final : public AstVisitor<CCodeGenerator> {
 public:
  explicit CCodeGenerator(uintptr_t stack_limit);
  ~CCodeGenerator();

  // The following routines print a node into a string.
  // The result string is alive as long as the AstPrinter is alive.
  const char* PrintProgram(FunctionLiteral* program);
  const char* PrintFunctionDeclaration(FunctionLiteral* function);
  const char* Finish();

  void PRINTF_FORMAT(2, 3) Print(const char* format, ...);

  // Individual nodes
#define DECLARE_VISIT(type) void Visit##type(type* node);
  AST_NODE_LIST(DECLARE_VISIT)
#undef DECLARE_VISIT

 private:
  friend class CIndentedScope;

  void Init();

  void PrintLabels(ZonePtrList<const AstRawString>* labels);
  void PrintLiteral(const AstRawString* value, bool quote);
  void PrintLiteral(const AstConsString* value, bool quote);
  void PrintLiteral(Literal* literal, bool quote);
  void PrintIndented(const char* txt);
  void PrintIndentedVisit(const char* s, AstNode* node);

  void PrintStatements(const ZonePtrList<Statement>* statements);
  void PrintDeclarations(Declaration::List* declarations);
  void PrintParameters(DeclarationScope* scope);
  void PrintArguments(const ZonePtrList<Expression>* arguments);
  void PrintCaseClause(CaseClause* clause);
  void PrintLiteralIndented(const char* info, Literal* literal, bool quote);
  void PrintLiteralIndented(const char* info, const AstRawString* value,
                            bool quote);
  void PrintLiteralIndented(const char* info, const AstConsString* value,
                            bool quote);
  void PrintLiteralWithModeIndented(const char* info, Variable* var,
                                    const AstRawString* value);
  void PrintLabelsIndented(ZonePtrList<const AstRawString>* labels,
                           const char* prefix = "");
  void PrintObjectProperties(
      const ZonePtrList<ObjectLiteral::Property>* properties);
  void PrintClassProperty(ClassLiteral::Property* property);
  void PrintClassProperties(
      const ZonePtrList<ClassLiteral::Property>* properties);
  void PrintClassStaticElements(
      const ZonePtrList<ClassLiteral::StaticElement>* static_elements);

  void inc_indent() { indent_++; }
  void dec_indent() { indent_--; }

  DEFINE_AST_VISITOR_SUBCLASS_MEMBERS();

  char* output_;  // output string buffer
  int size_;      // output_ size
  int pos_;       // current printing position
  int indent_;
  int c_file_fd_;
};

}  // namespace internal
}  // namespace v8

#endif
