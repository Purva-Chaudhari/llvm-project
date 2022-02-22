//===--- tools/clang-repl/ClangRepl.cpp - clang-repl - the Clang REPL -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements a REPL tool on top of clang.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Interpreter/Interpreter.h"

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/LineEditor/LineEditor.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h" // llvm_shutdown
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h" // llvm::Initialize*

static llvm::cl::opt<bool> OptRecovery("recovery",
                                       llvm::cl::Hidden);
static llvm::cl::list<std::string>
    ClangArgs("Xcc", llvm::cl::ZeroOrMore,
              llvm::cl::desc("Argument to pass to the CompilerInvocation"),
              llvm::cl::CommaSeparated);
static llvm::cl::opt<bool> OptHostSupportsJit("host-supports-jit",
                                              llvm::cl::Hidden);
static llvm::cl::list<std::string> OptInputs(llvm::cl::Positional,
                                             llvm::cl::ZeroOrMore,
                                             llvm::cl::desc("[code to run]"));

static void LLVMErrorHandler(void *UserData, const char *Message,
                             bool GenCrashDiag) {
  auto &Diags = *static_cast<clang::DiagnosticsEngine *>(UserData);

  Diags.Report(clang::diag::err_fe_error_backend) << Message;

  // Run the interrupt handlers to make sure any special cleanups get done, in
  // particular that we remove files registered with RemoveFileOnSignal.
  llvm::sys::RunInterruptHandlers();

  // We cannot recover from llvm errors.  When reporting a fatal error, exit
  // with status 70 to generate crash diagnostics.  For BSD systems this is
  // defined as an internal software error. Otherwise, exit with status 1.

  exit(GenCrashDiag ? 70 : 1);
}

static void adjustClangArgs(llvm::cl::list<std::string> &ClangArgs) {
  // Prepending -c to force the driver to do something if no action was
  // specified. By prepending we allow users to override the default
  // action and use other actions in incremental mode.
  // FIXME: Print proper driver diagnostics if the driver flags are wrong.
  ClangArgs.insert(ClangArgs.begin() + 1, "-c");

  if (!llvm::is_contained(ClangArgs, " -x") && !OptInputs.empty()) {
    // We do C++ by default; append right after argv[0] if no "-x" given
    ClangArgs.push_back("-x");
    ClangArgs.push_back("c++");
  }

  // Put a dummy C++ file on to ensure there's at least one compile job for the
  // driver to construct.
  ClangArgs.push_back("<<< inputs >>>");
}

llvm::ExitOnError ExitOnErr;
int main(int argc, const char **argv) {
  ExitOnErr.setBanner("clang-repl: ");
  llvm::cl::ParseCommandLineOptions(argc, argv);

  // If we don't know ClangArgv0 or the address of main() at this point, try
  // to guess it anyway (it's possible on some platforms).
  std::string MainExecutableName =
      llvm::sys::fs::getMainExecutable(nullptr, nullptr);

  ClangArgs.insert(ClangArgs.begin(), MainExecutableName.c_str());

  adjustClangArgs(ClangArgs);

  std::vector<const char *> ClangArgv(ClangArgs.size());
  std::transform(ClangArgs.begin(), ClangArgs.end(), ClangArgv.begin(),
                 [](const std::string &s) -> const char * { return s.data(); });

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();

  if (OptHostSupportsJit) {
    auto J = llvm::orc::LLJITBuilder().create();
    if (J)
      llvm::outs() << "true\n";
    else {
      llvm::consumeError(J.takeError());
      llvm::outs() << "false\n";
    }
    return 0;
  }

  // FIXME: Investigate if we could use runToolOnCodeWithArgs from tooling. It
  // can replace the boilerplate code for creation of the compiler instance.
  auto CI = ExitOnErr(clang::IncrementalCompilerBuilder::create(ClangArgv));

  // Set an error handler, so that any LLVM backend diagnostics go through our
  // error handler.
  llvm::install_fatal_error_handler(LLVMErrorHandler,
                                    static_cast<void *>(&CI->getDiagnostics()));

  // Load any requested plugins.
  CI->LoadRequestedPlugins();

  auto Interp = ExitOnErr(clang::Interpreter::create(std::move(CI)));

  if (OptRecovery) {
    assert(OptInputs.size() == 1 && "We only support a single input for now");
    llvm::StringRef File = OptInputs[0];
    // Parse first time.
    auto PTU = Interp->Parse("#include \"" + File.str() + "\"");
    if (auto Err = PTU.takeError())
      llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "recovery: ");

    // Restore logic goes here:
    Interp->Restore(*PTU);
    // Re-parseing the same file should be ok.
    PTU = Interp->Parse("#include \"" + File.str() + "\"");
    if (auto Err = PTU.takeError())
      llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "recovery: ");
  } else {
    for (const std::string &input : OptInputs) {
      if (auto Err = Interp->ParseAndExecute(input))
        llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "error: ");
    }
  }

  if (OptInputs.empty()) {
    llvm::LineEditor LE("clang-repl");
    // FIXME: Add LE.setListCompleter
    while (llvm::Optional<std::string> Line = LE.readLine()) {
      if (*Line == "quit")
        break;
      if (auto Err = Interp->ParseAndExecute(*Line))
        llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "error: ");
    }
  }

  // Our error handler depends on the Diagnostics object, which we're
  // potentially about to delete. Uninstall the handler now so that any
  // later errors use the default handling behavior instead.
  llvm::remove_fatal_error_handler();

  llvm::llvm_shutdown();

  return 0;
}
