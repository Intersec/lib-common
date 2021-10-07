//===--- RewriteObjC.cpp - Playground for the code rewriter ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Hacks and fun related to the code rewriter.
//
//===----------------------------------------------------------------------===//

#ifndef IS_CLANG_REWRITE_BLOCKS_HPP
#define IS_CLANG_REWRITE_BLOCKS_HPP

#include <clang/Rewrite/Frontend/ASTConsumers.h>
#include <clang/AST/AST.h>
#include <clang/AST/ASTConsumer.h>

namespace clang {
std::unique_ptr<ASTConsumer>
CreateBlocksRewriter(const std::string &InFile, std::unique_ptr<raw_ostream> OS,
                     DiagnosticsEngine &Diags, const LangOptions &LOpts,
                     bool SilenceRewriteMacroWarning);
};

#endif /* IS_CLANG_REWRITE_BLOCKS_HPP */

// vim:set ts=2 sts=2:
