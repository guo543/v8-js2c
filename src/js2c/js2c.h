#ifndef V8_JS2C_H_
#define V8_JS2C_H_

#include "include/libplatform/libplatform.h"
#include "include/v8-context.h"
#include "include/v8-initialization.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-primitive.h"
#include "include/v8-script.h"
#include "src/api/api.h"
#include "src/ast/ast.h"
#include "src/codegen/script-details.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/flags/flags.h"

namespace v8 {

class JS2C {
 public:
  static void GenerateCCode(Local<Context> context,
                            ScriptCompiler::Source* source);
};

}  // namespace v8

#endif
