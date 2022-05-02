/***************************************************************************/
/*                                                                         */
/* Copyright 2022 INTERSEC SA                                              */
/*                                                                         */
/* Licensed under the Apache License, Version 2.0 (the "License");         */
/* you may not use this file except in compliance with the License.        */
/* You may obtain a copy of the License at                                 */
/*                                                                         */
/*     http://www.apache.org/licenses/LICENSE-2.0                          */
/*                                                                         */
/* Unless required by applicable law or agreed to in writing, software     */
/* distributed under the License is distributed on an "AS IS" BASIS,       */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*/
/* See the License for the specific language governing permissions and     */
/* limitations under the License.                                          */
/*                                                                         */
/***************************************************************************/

//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

/*
 * This file is adapted from `clang/tools/driver/cc1_main.cpp` and
 * `clang/lib/FrontendTool/ExecuteCompilerInvocation.cpp`.
 */

#include <clang/Basic/DiagnosticFrontend.h>
#include <clang/Basic/Version.h>
#include <clang/Driver/DriverDiagnostic.h>
#include <clang/Driver/Options.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Frontend/TextDiagnosticBuffer.h>
#include <llvm/Option/Option.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Timer.h>
#include <llvm/Support/TimeProfiler.h>
#include <cstdlib>
#include <cstdio>

#include "RewriteBlocks.hpp"

using namespace clang;
using namespace llvm::opt;


namespace {

std::string GetExecutablePath(const char *Argv0, void *MainAddr) {
  return llvm::sys::fs::getMainExecutable(Argv0, MainAddr);
}

void LLVMErrorHandler(void *UserData, const std::string &Message,
                      bool GenCrashDiag) {
  DiagnosticsEngine &Diags = *static_cast<DiagnosticsEngine*>(UserData);

  Diags.Report(diag::err_fe_error_backend) << Message;

  // Run the interrupt handlers to make sure any special cleanups get done, in
  // particular that we remove files registered with RemoveFileOnSignal.
  llvm::sys::RunInterruptHandlers();

  // We cannot recover from llvm errors.  When reporting a fatal error, exit
  // with status 70 to generate crash diagnostics.  For BSD systems this is
  // defined as an internal software error.  Otherwise, exit with status 1.
  exit(GenCrashDiag ? 70 : 1);
}

class RewriteBlocksAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;
};

std::unique_ptr<ASTConsumer>
RewriteBlocksAction::CreateASTConsumer(CompilerInstance &CI,
                                       StringRef InFile) {
  std::string suffix("rw." + llvm::sys::path::extension(InFile).str());

  if (std::unique_ptr<raw_ostream> OS =
      CI.createDefaultOutputFile(false, InFile, suffix))
    return CreateBlocksRewriter(std::string(InFile), std::move(OS),
                                CI.getDiagnostics(), CI.getLangOpts(),
                                CI.getDiagnosticOpts().NoRewriteMacros);
  return nullptr;
}

static bool ExecuteCompilerInvocationRewriteBlock(CompilerInstance *Clang) {
  // Honor -help.
  if (Clang->getFrontendOpts().ShowHelp) {
#if CLANG_VERSION_MAJOR >= 13
    driver::getDriverOptTable().printHelp(
#elif CLANG_VERSION_MAJOR >= 10
    driver::getDriverOptTable().PrintHelp(
#else
    std::unique_ptr<OptTable> Opts = driver::createDriverOptTable();
    Opts->PrintHelp(
#endif /* CLANG_VERSION_MAJOR >= 13 || CLANG_VERSION_MAJOR >= 10 */
      llvm::outs(),
      "clang-rewrite-blocks [options] file.blk -o file.blk.c",
      "LLVM 'Clang' Compiler Rewriter Block: "
      "http://clang.llvm.org http://intersec.com",
      /*Include=*/driver::options::CC1Option,
      /*Exclude=*/0, /*ShowAllAliases=*/false);
    return true;
  }

  // Honor -version.
  //
  // FIXME: Use a better -version message?
  if (Clang->getFrontendOpts().ShowVersion) {
    llvm::cl::PrintVersionMessage();
    return true;
  }

  // Load any requested plugins.
  for (unsigned i = 0,
         e = Clang->getFrontendOpts().Plugins.size(); i != e; ++i) {
    const std::string &Path = Clang->getFrontendOpts().Plugins[i];
    std::string Error;
    if (llvm::sys::DynamicLibrary::LoadLibraryPermanently(Path.c_str(), &Error))
      Clang->getDiagnostics().Report(diag::err_fe_unable_to_load_plugin)
        << Path << Error;
  }

  // Check if any of the loaded plugins replaces the main AST action
  for (FrontendPluginRegistry::iterator it = FrontendPluginRegistry::begin(),
                                        ie = FrontendPluginRegistry::end();
       it != ie; ++it) {
    std::unique_ptr<PluginASTAction> P(it->instantiate());
    if (P->getActionType() == PluginASTAction::ReplaceAction) {
      Clang->getFrontendOpts().ProgramAction = clang::frontend::PluginAction;
#if CLANG_VERSION_MAJOR >= 11
      Clang->getFrontendOpts().ActionName = it->getName().str();
#else
      Clang->getFrontendOpts().ActionName = it->getName();
#endif /* CLANG_VERSION_MAJOR >= 11 */
      break;
    }
  }

  // If there were errors in processing arguments, don't do anything else.
  if (Clang->getDiagnostics().hasErrorOccurred())
    return false;
  // Create and execute the frontend action.
#if CLANG_VERSION_MAJOR >= 10
  std::unique_ptr<FrontendAction> Act(std::make_unique<RewriteBlocksAction>());
#else
  std::unique_ptr<FrontendAction> Act(llvm::make_unique<RewriteBlocksAction>());
#endif /* CLANG_VERSION_MAJOR >= 10 */
  if (!Act)
    return false;
  bool Success = Clang->ExecuteAction(*Act);
  if (Clang->getFrontendOpts().DisableFree)
    llvm::BuryPointer(std::move(Act));
  return Success;
}

};

int main(int argc, const char **argv) {
  const char *Argv0 = argv[0];

  if (argc <= 1) {
    fprintf(stderr, "%s: error: no input files\n", Argv0);
    exit(1);
  }

  void *MainAddr = (void*) (intptr_t) GetExecutablePath;

  // Initialize targets first, so that --version shows registered targets.
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

  // Buffer diagnostics from argument parsing so that we can output them using a
  // well formed diagnostic object.
  std::unique_ptr<CompilerInstance> Clang(new CompilerInstance());
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
  TextDiagnosticBuffer *DiagsBuffer = new TextDiagnosticBuffer;
  TextDiagnosticPrinter *DiagClient =
    new TextDiagnosticPrinter(llvm::errs(), &*DiagOpts);
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  DiagnosticsEngine Diags(DiagID, &*DiagOpts, DiagClient);

  ArrayRef<const char*> Args(argv + 1, argv + argc);

#if CLANG_VERSION_MAJOR >= 10
  bool Success = CompilerInvocation::CreateFromArgs(
      Clang->getInvocation(), Args, Diags);
#else
  bool Success = CompilerInvocation::CreateFromArgs(
      Clang->getInvocation(), Args.begin(), Args.end(), Diags);
#endif /* CLANG_VERSION_MAJOR >= 10 */

  if (Clang->getFrontendOpts().TimeTrace) {
#if CLANG_VERSION_MAJOR >= 10
    llvm::timeTraceProfilerInitialize(
        Clang->getFrontendOpts().TimeTraceGranularity, Argv0);
#else
    llvm::timeTraceProfilerInitialize();
#endif /* CLANG_VERSION_MAJOR >= 10 */
  }

  // Infer the builtin include path if unspecified.
  if (Clang->getHeaderSearchOpts().UseBuiltinIncludes &&
      Clang->getHeaderSearchOpts().ResourceDir.empty())
    Clang->getHeaderSearchOpts().ResourceDir =
      CompilerInvocation::GetResourcesPath(Argv0, MainAddr);

  // Create the actual diagnostics engine.
  Clang->createDiagnostics();
  if (!Clang->hasDiagnostics())
    return 1;

  // Set an error handler, so that any LLVM backend diagnostics go through our
  // error handler.
  llvm::install_fatal_error_handler(LLVMErrorHandler,
                                  static_cast<void*>(&Clang->getDiagnostics()));

  DiagsBuffer->FlushDiagnostics(Clang->getDiagnostics());
  if (!Success)
    return 1;

  // Execute the frontend actions.
  {
    llvm::TimeTraceScope TimeScope("ExecuteCompiler", StringRef(""));
    Success = ExecuteCompilerInvocationRewriteBlock(Clang.get());
  }

  // If any timers were active but haven't been destroyed yet, print their
  // results now.  This happens in -disable-free mode.
  llvm::TimerGroup::printAll(llvm::errs());

  if (llvm::timeTraceProfilerEnabled()) {
    SmallString<128> Path(Clang->getFrontendOpts().OutputFile);
    llvm::sys::path::replace_extension(Path, "json");
#if CLANG_VERSION_MAJOR >= 12
    auto profilerOutput = Clang->createOutputFile(
        Path.str(), /*Binary=*/false, /*RemoveFileOnSignal=*/false,
        /*useTemporary=*/false);
#else
    auto profilerOutput =
        Clang->createOutputFile(Path.str(),
                                /*Binary=*/false,
                                /*RemoveFileOnSignal=*/false, "",
                                /*Extension=*/"json",
                                /*useTemporary=*/false);
#endif /* CLANG_VERSION_MAJOR >= 12 */

    if (profilerOutput) {
        llvm::timeTraceProfilerWrite(*profilerOutput);
        // FIXME(ibiryukov): make profilerOutput flush in destructor instead.
        profilerOutput->flush();
        llvm::timeTraceProfilerCleanup();
#if CLANG_VERSION_MAJOR >= 12
        Clang->clearOutputFiles(false);
#endif /* CLANG_VERSION_MAJOR >= 12 */

        llvm::errs() << "Time trace json-file dumped to " << Path.str() << "\n";
        llvm::errs()
            << "Use chrome://tracing or Speedscope App "
               "(https://www.speedscope.app) for flamegraph visualization\n";
    }
  }

  // Our error handler depends on the Diagnostics object, which we're
  // potentially about to delete. Uninstall the handler now so that any
  // later errors use the default handling behavior instead.
  llvm::remove_fatal_error_handler();

  // When running with -disable-free, don't do any destruction or shutdown.
  if (Clang->getFrontendOpts().DisableFree) {
    llvm::BuryPointer(std::move(Clang));
    return !Success;
  }

  return !Success;
}

// vim:set ts=2 sts=2:
