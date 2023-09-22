// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/js2c/js2c.h"

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
#include "src/api/api-inl.h"
#include "src/ast/ast.h"
#include "src/js2c/c-code-generator.h"
#include "src/codegen/script-details.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/flags/flags.h"
#include "src/parsing/parsing.h"
#include "src/objects/script.h"

namespace v8 {

namespace {
i::ScriptDetails GetScriptDetails(
    i::Isolate* i_isolate, Local<Value> resource_name, int resource_line_offset,
    int resource_column_offset, Local<Value> source_map_url,
    Local<Data> host_defined_options, ScriptOriginOptions origin_options) {
  i::ScriptDetails script_details(Utils::OpenHandle(*(resource_name), true),
                                  origin_options);
  script_details.line_offset = resource_line_offset;
  script_details.column_offset = resource_column_offset;
  script_details.host_defined_options =
      host_defined_options.IsEmpty()
          ? i_isolate->factory()->empty_fixed_array()
          : Utils::OpenHandle(*(host_defined_options));
  if (!source_map_url.IsEmpty()) {
    script_details.source_map_url = Utils::OpenHandle(*(source_map_url));
  }
  return script_details;
}

void SetScriptFieldsFromDetails(i::Isolate* isolate, i::Script script,
                                i::ScriptDetails script_details,
                                i::DisallowGarbageCollection* no_gc) {
  i::Handle<i::Object> script_name;
  if (script_details.name_obj.ToHandle(&script_name)) {
    script.set_name(*script_name);
    script.set_line_offset(script_details.line_offset);
    script.set_column_offset(script_details.column_offset);
  }
  // The API can provide a source map URL, but a source map URL could also have
  // been inferred by the parser from a magic comment. The latter takes
  // preference over the former, so we don't want to override the source mapping
  // URL if it already exists.
  i::Handle<i::Object> source_map_url;
  if (script_details.source_map_url.ToHandle(&source_map_url) &&
      script.source_mapping_url(isolate).IsUndefined(isolate)) {
    script.set_source_mapping_url(*source_map_url);
  }
  i::Handle<i::Object> host_defined_options;
  if (script_details.host_defined_options.ToHandle(&host_defined_options)) {
    // TODO(cbruni, chromium:1244145): Remove once migrated to the context.
    if (host_defined_options->IsFixedArray()) {
      script.set_host_defined_options(i::FixedArray::cast(*host_defined_options));
    }
  }
}

i::Handle<i::Script> NewScript(
    i::Isolate* isolate, i::ParseInfo* parse_info, i::Handle<i::String> source,
    i::ScriptDetails script_details, i::NativesFlag natives,
    i::MaybeHandle<i::FixedArray> maybe_wrapped_arguments = i::kNullMaybeHandle) {
  // Create a script object describing the script to be compiled.
  i::Handle<i::Script> script =
      parse_info->CreateScript(isolate, source, maybe_wrapped_arguments,
                               script_details.origin_options, natives);
  i::DisallowGarbageCollection no_gc;
  SetScriptFieldsFromDetails(isolate, *script, script_details, &no_gc);
  return script;
}
}  // namespace

void PerformJS2C(i::ParseInfo* parse_info, i::FunctionLiteral* literal, std::ofstream& ofstream_c, std::ofstream& ofstream_h) {
  i::StdoutStream os;
  std::unique_ptr<char[]> name = literal->GetDebugName();
  // os << "[generating C code for function: " << name.get() << "]" << std::endl;

  ofstream_c << i::CCodeGenerator(parse_info->stack_limit()).PrintProgram(literal)
           << std::endl;
  ofstream_h << i::CCodeGenerator(parse_info->stack_limit())
                  .PrintFunctionDeclaration(literal)
           << std::endl;
}

void FinishJS2C(i::ParseInfo* parse_info, std::ofstream& ofstream) {
  // i::StdoutStream os;
  // os << "[finishing generation of C code]" << std::endl;

  ofstream << i::CCodeGenerator(parse_info->stack_limit()).Finish() << std::endl;

}

void JS2C::GenerateCCode(Local<Context> context,
                         ScriptCompiler::Source* source) {
  auto isolate =
      reinterpret_cast<v8::internal::Isolate*>(context->GetIsolate());
  i::ScriptDetails script_details = GetScriptDetails(
      isolate, source->resource_name, source->resource_line_offset,
      source->resource_column_offset, source->source_map_url,
      source->host_defined_options, source->resource_options);
  i::Handle<i::String> str = Utils::OpenHandle(*(source->source_string));
  i::UnoptimizedCompileState compile_state;
  i::ReusableUnoptimizedCompileState reusable_state(isolate);
  i::LanguageMode language_mode =
      construct_language_mode(i::v8_flags.use_strict);
  i::UnoptimizedCompileFlags flags =
      i::UnoptimizedCompileFlags::ForToplevelCompile(
          isolate, true, language_mode, script_details.repl_mode,
          script_details.origin_options.IsModule() ? ScriptType::kModule
                                                   : ScriptType::kClassic,
          false);

  flags.set_is_eager(true);

  i::ParseInfo parse_info(isolate, flags, &compile_state, &reusable_state);
  i::Handle<i::Script> script = NewScript(
      isolate, &parse_info, str, script_details, i::NOT_NATIVES_CODE);
  bool result = i::parsing::ParseProgram(
      &parse_info, script, isolate, i::parsing::ReportStatisticsMode::kYes);

  printf("%s\n", result ? "success" : "failure");

  std::vector<i::FunctionLiteral*> functions_to_compile;
  functions_to_compile.push_back(parse_info.literal());

  std::ofstream ofstream_h;
  std::ofstream ofstream_c;
  ofstream_h.open("test.h");
  ofstream_c.open("test.c");
  ofstream_c << "#include \"test.h\"" << std::endl
             << "#include <stdio.h>" << std::endl
             << std::endl;

  while (!functions_to_compile.empty()) {
    i::FunctionLiteral* literal = functions_to_compile.back();
    functions_to_compile.pop_back();

    // TODO: Give the functions vector to AST visitor
    PerformJS2C(&parse_info, literal, ofstream_c, ofstream_h);
  }

  FinishJS2C(&parse_info, ofstream_c);
  ofstream_c.close();
  ofstream_h.close();
}

}  // namespace v8

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
      // v8::Local<v8::Script> script =

      v8::JS2C::GenerateCCode(context, &source);
      // v8::ScriptCompiler::Compile(
      //     context, &source, v8::ScriptCompiler::CompileOptions::kEagerCompile,
      //     v8::ScriptCompiler::NoCacheReason::kNoCacheNoReason)
      //     .ToLocalChecked();

      // // Run the script to get the result.
      // v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();

      // // Convert the result to an UTF8 string and print it.
      // v8::String::Utf8Value utf8(isolate, result);
      // printf("%s\n", *utf8);
    }
  }
  // Dispose the isolate and tear down V8.
  isolate->Dispose();
  v8::V8::Dispose();
  v8::V8::DisposePlatform();
  delete create_params.array_buffer_allocator;
  return 0;
}
