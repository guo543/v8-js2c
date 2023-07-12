// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>

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

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Please specify a file to compile.\n");
    return 1;
  }

  const char* filename = argv[1];

  // Initialize V8.
  v8::V8::InitializeICUDefaultLocation(argv[0]);
  v8::V8::InitializeExternalStartupData(argv[0]);
  std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
  // v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
  v8::V8::SetFlagsFromString("--js2c");
  v8::V8::SetFlagsFromString("--print-ast");
  v8::V8::InitializePlatform(platform.get());
  v8::V8::Initialize();

  // Create a new Isolate and make it the current one.
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator =
      v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  {
    v8::Isolate::Scope isolate_scope(isolate);

    // Create a stack-allocated handle scope.
    v8::HandleScope handle_scope(isolate);

    // Create a new context.
    v8::Local<v8::Context> context = v8::Context::New(isolate);

    // Enter the context for compiling and running the hello world script.
    v8::Context::Scope context_scope(context);

    {
      // printf("%s\n", filename);
      std::ifstream ifstream;
      ifstream.open(filename);
      if (ifstream.fail()) {
        fprintf(stderr, "Error opening file: %s\n", filename);
        exit(1);
      }
      std::string cpp_code;
      std::ostringstream ss;
      ss << ifstream.rdbuf();
      cpp_code = ss.str();

      // Create a string containing the JavaScript source code.
      v8::Local<v8::String> source_string =
          v8::String::NewFromUtf8(isolate, cpp_code.c_str()).ToLocalChecked();
      v8::ScriptCompiler::Source source(source_string);
      // Compile the source code.
      v8::Local<v8::Script> script = v8::ScriptCompiler::Compile(
          context, &source, v8::ScriptCompiler::CompileOptions::kEagerCompile,
          v8::ScriptCompiler::NoCacheReason::kNoCacheNoReason
              ).ToLocalChecked();

      // Run the script to get the result.
      v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();

      // Convert the result to an UTF8 string and print it.
      v8::String::Utf8Value utf8(isolate, result);
      printf("%s\n", *utf8);
    }
  }
  // Dispose the isolate and tear down V8.
  isolate->Dispose();
  v8::V8::Dispose();
  v8::V8::DisposePlatform();
  delete create_params.array_buffer_allocator;
  return 0;
}
