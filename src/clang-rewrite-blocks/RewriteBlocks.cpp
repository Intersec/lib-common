/***************************************************************************/
/*                                                                         */
/* Copyright 2021 INTERSEC SA                                              */
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

//===--- RewriteBlocks.cpp - Playground for the code rewriter -------------===//
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

/*
 * This file is adapted from `clang/lib/Frontend/Rewrite/RewriteObjC.cpp`.
 */

#include <clang/Rewrite/Frontend/ASTConsumers.h>
#include <clang/AST/AST.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Attr.h>
#include <clang/AST/ParentMap.h>
#include <clang/Basic/CharInfo.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/IdentifierTable.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/Version.h>
#include <clang/Lex/Lexer.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>

using namespace clang;
using llvm::utostr;

namespace clang {
std::unique_ptr<ASTConsumer>
CreateBlocksRewriter(const std::string &InFile, std::unique_ptr<raw_ostream> OS,
                     DiagnosticsEngine &Diags, const LangOptions &LOpts,
                     bool SilenceRewriteMacroWarning);
};

namespace {
  class RewriteBlocks : public ASTConsumer {
  protected:

    enum {
      BLOCK_FIELD_IS_OBJECT   =  3,  /* id, NSObject, __attribute__((NSObject)),
                                        block, ... */
      BLOCK_FIELD_IS_BLOCK    =  7,  /* a block variable */
      BLOCK_FIELD_IS_BYREF    =  8,  /* the on stack structure holding the
                                        __block variable */
      BLOCK_FIELD_IS_WEAK     = 16,  /* declared __weak, only used in byref copy
                                        helpers */
      BLOCK_BYREF_CALLER      = 128, /* called from __block (byref) copy/dispose
                                        support routines */
      BLOCK_BYREF_CURRENT_MAX = 256
    };

    enum {
      BLOCK_NEEDS_FREE =        (1 << 24),
      BLOCK_HAS_COPY_DISPOSE =  (1 << 25),
      BLOCK_HAS_CXX_OBJ =       (1 << 26),
      BLOCK_IS_GC =             (1 << 27),
      BLOCK_IS_GLOBAL =         (1 << 28),
      BLOCK_HAS_DESCRIPTOR =    (1 << 29)
    };

    Rewriter Rewrite;
    DiagnosticsEngine &Diags;
    const LangOptions &LangOpts;
    ASTContext *Context;
    SourceManager *SM;
    TranslationUnitDecl *TUDecl;
    FileID MainFileID;
    const char *MainFileStart, *MainFileEnd;
    Stmt *CurrentBody;
    std::string InFileName;
    std::unique_ptr<raw_ostream> OutFile;
    std::string Preamble;

    FunctionDecl *CurFunctionDef;
    FunctionDecl *CurFunctionDeclToDeclareForBlock;
    SmallVector<BlockExpr *, 32> Blocks;
    SmallVector<int, 32> InnerDeclRefsCount;
    SmallVector<DeclRefExpr *, 32> InnerDeclRefs;
    SmallVector<DeclRefExpr *, 32> BlockDeclRefs;
    VarDecl *GlobalVarDecl;
    llvm::DenseSet<uint64_t> CopyDestroyCache;

    // Block related declarations.
    SmallVector<ValueDecl *, 8> BlockByCopyDecls;
    llvm::SmallPtrSet<ValueDecl *, 8> BlockByCopyDeclsPtrSet;
    SmallVector<ValueDecl *, 8> BlockByRefDecls;
    llvm::SmallPtrSet<ValueDecl *, 8> BlockByRefDeclsPtrSet;
    llvm::DenseMap<ValueDecl *, unsigned> BlockByRefDeclNo;
    llvm::SmallPtrSet<ValueDecl *, 8> ImportedBlockDecls;
    llvm::SmallPtrSet<VarDecl *, 8> ImportedLocalExternalDecls;

    llvm::DenseMap<BlockExpr *, std::string> RewrittenBlockExprs;

    // This maps an original source AST to it's rewritten form. This allows
    // us to avoid rewriting the same node twice (which is very uncommon).
    // This is needed to support some of the exotic property rewriting.
    llvm::DenseMap<Stmt *, Stmt *> ReplacedNodes;

    unsigned RewriteFailedDiag;

    bool IsHeader;
    bool SilenceRewriteMacroWarning;
    bool DisableReplaceStmt;
    class DisableReplaceStmtScope {
      RewriteBlocks &R;
      bool SavedValue;

    public:
      DisableReplaceStmtScope(RewriteBlocks &R)
        : R(R), SavedValue(R.DisableReplaceStmt) {
        R.DisableReplaceStmt = true;
      }
      ~DisableReplaceStmtScope() {
        R.DisableReplaceStmt = SavedValue;
      }
    };

  public:
    RewriteBlocks(std::string inFile, std::unique_ptr<raw_ostream> OS,
                  DiagnosticsEngine &D, const LangOptions &LOpts,
                  bool silenceMacroWarn);

    ~RewriteBlocks() override {}

    virtual void Initialize(ASTContext &Context) override;
    virtual bool HandleTopLevelDecl(DeclGroupRef D) override;
    virtual void HandleTranslationUnit(ASTContext &C) override;

    CStyleCastExpr* NoTypeInfoCStyleCastExpr(ASTContext *Ctx, QualType Ty,
                                             CastKind Kind, Expr *E);
    bool convertBlockPointerToFunctionPointer(QualType &T);
    QualType canonifyType(QualType t);
    QualType convertFunctionTypeOfBlocks(const FunctionType *FT);
    QualType getSimpleFunctionType(QualType result,
                                   ArrayRef<QualType> args,
                                   bool variadic = false);
    void GetExtentOfArgList(const char *Name, const char *&LParen,
                            const char *&RParen);
    void GetBlockDeclRefExprs(Stmt *S);
    void GetInnerBlockDeclRefExprs(Stmt *S,
                SmallVectorImpl<DeclRefExpr *> &InnerBlockDeclRefs,
                llvm::SmallPtrSetImpl<const DeclContext *> &InnerContexts);
    bool PointerTypeTakesAnyBlockArguments(QualType QT);
    void RewriteBlockPointerType(std::string& Str, QualType Type);
    void RewriteBlockPointerTypeVariable(std::string& Str, ValueDecl *VD);
    void RewriteBlockPointerFunctionArgs(FunctionDecl *FD);
    void RewriteBlockPointerDecl(NamedDecl *ND);
    void RewriteBlocksInFunctionProtoType(QualType funcType, NamedDecl *D);
    Stmt *RewriteBlockDeclRefExpr(DeclRefExpr *DeclRefExp);
    Stmt *RewriteLocalVariableExternalStorage(DeclRefExpr *DRE);
    void RewriteCastExpr(CStyleCastExpr *CE);
    void RewriteRecordBody(RecordDecl *RD);
    void CheckFunctionPointerDecl(QualType funcType, NamedDecl *ND);
    Stmt *RewriteStatement(Stmt *S, CompoundStmt *CS);
    void RewriteBlockLiteralFunctionDecl(FunctionDecl *FD);
    void RewriteByRefString(std::string &ResultStr,
                            const std::string &Name,
                            ValueDecl *VD, bool def = false);
    void RewriteByRefVar(VarDecl *ND);
    std::string SynthesizeByrefCopyDestroyHelper(VarDecl *VD, int flag);
    std::string SynthesizeBlockFunc(BlockExpr *CE, int i,
                                    StringRef funcName,
                                    std::string Tag);
    std::string SynthesizeBlockHelperFuncs(BlockExpr *CE, int i,
                                           StringRef funcName,
                                           std::string Tag);
    std::string SynthesizeBlockImpl(BlockExpr *CE, std::string Tag,
                                    std::string Desc);
    std::string SynthesizeBlockDescriptor(std::string DescTag,
                                          std::string ImplTag, int i,
                                          StringRef FunName,
                                          unsigned hasCopy);
    FunctionDecl *SynthBlockInitFunctionDecl(StringRef name);
    Stmt *SynthesizeBlockCall(CallExpr *Exp, const Expr *BlockExp);
    Stmt *SynthBlockInitExpr(BlockExpr *Exp,
                             const SmallVector<DeclRefExpr *, 8> &InnerBlockDeclRefs,
                             CompoundStmt *CS);
    void SynthesizeBlockLiterals(SourceLocation FunLocStart, StringRef FunName);
    void InsertBlockLiteralsWithinFunction(FunctionDecl *FD);
    void CollectBlockDeclRefInfo(BlockExpr *Exp);
    void HandleDeclInMainFile(Decl *D);
    bool HandleTopLevelSingleDecl(Decl *D);

    void ReplaceStmt(Stmt *Old, Stmt *New, bool doSharpLine = false) {
      ReplaceStmtWithRange(Old, New, Old->getSourceRange(), InFileName,
                           doSharpLine);
    }

    void ReplaceStmtWithRange(Stmt *Old, Stmt *New, SourceRange SrcRange,
                              std::string File = "", bool doSharpLine = false)
    {
      assert(Old != nullptr && New != nullptr && "Expected non-null Stmt's");

      Stmt *ReplacingStmt = ReplacedNodes[Old];
      if (ReplacingStmt)
        return; // We can't rewrite the same node twice.

      if (DisableReplaceStmt)
        return;

      // Measure the old text.
      int Size = Rewrite.getRangeSize(SrcRange);
      if (Size == -1) {
        Diags.Report(Context->getFullLoc(Old->getBeginLoc()), RewriteFailedDiag)
                     << Old->getSourceRange();
        return;
      }
      // Get the new text.
      std::string SStr;
      llvm::raw_string_ostream S(SStr);
      New->printPretty(S, nullptr, PrintingPolicy(LangOpts));
      const std::string &Str = S.str();

      // If replacement succeeded or warning disabled return with no warning.
      if (Rewrite.ReplaceText(SrcRange.getBegin(), Size, Str)) {
          if (SilenceRewriteMacroWarning) {
              return;
          }
          Diags.Report(Context->getFullLoc(Old->getBeginLoc()),
                       RewriteFailedDiag)
              << Old->getSourceRange();
          return;
      }
      ReplacedNodes[Old] = New;
      if (doSharpLine) {
        unsigned f_lines =
          Rewrite.getSourceMgr().getExpansionLineNumber(Old->getEndLoc()) -
          Rewrite.getSourceMgr().getExpansionLineNumber(Old->getBeginLoc());
        unsigned t_lines = 0;
        size_t pos = Str.find('\n');

        while (pos != std::string::npos) {
          t_lines++;
          pos = Str.find('\n', pos + 1);
        }

        if (f_lines != t_lines) {
          std::string S = "\n# line ";

          S += utostr(Rewrite.getSourceMgr().getExpansionLineNumber(
              Old->getEndLoc()));
          S += " \"" + File + "\"\n";
          InsertText(Old->getEndLoc(), S);
        }
      }
    }

    void InsertText(SourceLocation Loc, StringRef Str,
                    bool InsertAfter = true) {
      // If insertion succeeded or warning disabled return with no warning.
      if (!Rewrite.InsertText(Loc, Str, InsertAfter) ||
          SilenceRewriteMacroWarning)
        return;

      Diags.Report(Context->getFullLoc(Loc), RewriteFailedDiag);
    }

    std::string MakeSharpLine(SourceLocation Loc) {
      std::string S;

      S += "# line ";
      S += utostr(SM->getExpansionLineNumber(Loc));
      S += " \"" + InFileName + "\"\n";

      return S;
    }

    void PutSharpLine(SourceLocation Start) {
      Rewrite.InsertText(Start, MakeSharpLine(Start), true);
    }

    void ReplaceText(SourceLocation Start, unsigned OrigLength,
                     StringRef Str, bool warn) {
      // If removal succeeded or warning disabled return with no warning.
      if (!Rewrite.ReplaceText(Start, OrigLength, Str) ||
          SilenceRewriteMacroWarning)
        return;

      if (warn) {
          Diags.Report(Context->getFullLoc(Start), RewriteFailedDiag);
      }
    }
  };
}

static bool IsHeaderFile(const std::string &Filename) {
  std::string::size_type DotPos = Filename.rfind('.');

  if (DotPos == std::string::npos) {
    // no file extension
    return false;
  }

  std::string Ext = std::string(Filename.begin()+DotPos+1, Filename.end());
  // C header: .h
  // C++ header: .hh or .H;
  return Ext == "h" || Ext == "hh" || Ext == "H";
}

static bool HasLocalVariableExternalStorage(ValueDecl *VD) {
  if (VarDecl *Var = dyn_cast<VarDecl>(VD))
    return (Var->isFunctionOrMethodVarDecl() && !Var->hasLocalStorage());
  return false;
}

RewriteBlocks::RewriteBlocks(std::string inFile, std::unique_ptr<raw_ostream> OS,
                             DiagnosticsEngine &D, const LangOptions &LOpts,
                             bool silenceMacroWarn)
      : Diags(D), LangOpts(LOpts), InFileName(inFile), OutFile(std::move(OS)),
        SilenceRewriteMacroWarning(silenceMacroWarn) {
  IsHeader = IsHeaderFile(inFile);
  RewriteFailedDiag = Diags.getCustomDiagID(DiagnosticsEngine::Warning,
               "rewriting sub-expression within a macro (may not be correct)");
}

std::unique_ptr<ASTConsumer>
clang::CreateBlocksRewriter(const std::string &InFile, std::unique_ptr<raw_ostream> OS,
                            DiagnosticsEngine &Diags, const LangOptions &LOpts,
                            bool SilenceRewriteMacroWarning) {
#if CLANG_VERSION_MAJOR >= 10
  return std::make_unique<RewriteBlocks>(InFile, std::move(OS), Diags, LOpts,
                                         SilenceRewriteMacroWarning);
#else
  return llvm::make_unique<RewriteBlocks>(InFile, std::move(OS), Diags, LOpts,
                                          SilenceRewriteMacroWarning);
#endif /* CLANG_VERSION_MAJOR >= 10 */
}

void RewriteBlocks::Initialize(ASTContext &context)
{
  Context = &context;
  SM = &Context->getSourceManager();
  TUDecl = Context->getTranslationUnitDecl();
  CurFunctionDef = 0;
  CurFunctionDeclToDeclareForBlock = 0;
  GlobalVarDecl = 0;
  CurrentBody = 0;
  DisableReplaceStmt = false;

  // Get the ID and start/end of the main file.
  MainFileID = SM->getMainFileID();
#if CLANG_VERSION_MAJOR >= 12
  llvm::MemoryBufferRef MainBuf = SM->getBufferOrFake(MainFileID);
  MainFileStart = MainBuf.getBufferStart();
  MainFileEnd = MainBuf.getBufferEnd();
#else
  const llvm::MemoryBuffer *MainBuf = SM->getBuffer(MainFileID);
  MainFileStart = MainBuf->getBufferStart();
  MainFileEnd = MainBuf->getBufferEnd();
#endif /* CLANG_VERSION_MAJOR >= 12 */

  Rewrite.setSourceMgr(Context->getSourceManager(), Context->getLangOpts());
  if (LangOpts.MicrosoftExt) {
    Preamble += "#define __OBJC_RW_DLLIMPORT extern \"C\" __declspec(dllimport)\n";
    Preamble += "#define __OBJC_RW_STATICIMPORT extern \"C\"\n";
  } else {
    if (LangOpts.CPlusPlus) {
      Preamble += "#define __OBJC_RW_DLLIMPORT extern \"C\"\n";
    } else {
      Preamble += "#define __OBJC_RW_DLLIMPORT extern\n";
    }
  }
  // Blocks preamble.
  Preamble += "#ifndef BLOCK_IMPL\n";
  Preamble += "#define BLOCK_IMPL\n";
  Preamble += "struct __block_impl {\n";
  Preamble += "  void *isa;\n";
  Preamble += "  int Flags;\n";
  Preamble += "  int Reserved;\n";
  Preamble += "  void *FuncPtr;\n";
  if (LangOpts.CPlusPlus) {
    Preamble += "  __block_impl(void *_isa, int _flags, void *_fp)\n";
    Preamble += "    : isa(_isa), Flags(_flags), Reserved(0), FuncPtr(_fp) { }\n";
  }
  Preamble += "};\n";
  Preamble += "// Runtime copy/destroy helper functions (from Block_private.h)\n";
  Preamble += "#ifdef __OBJC_EXPORT_BLOCKS\n";
  Preamble += "extern \"C\" __declspec(dllexport) "
  "void _Block_object_assign(void *, const void *, const int);\n";
  Preamble += "extern \"C\" __declspec(dllexport) void _Block_object_dispose(const void *, const int);\n";
  Preamble += "extern \"C\" __declspec(dllexport) void *_NSConcreteGlobalBlock[32];\n";
  Preamble += "extern \"C\" __declspec(dllexport) void *_NSConcreteStackBlock[32];\n";
  Preamble += "#else\n";
  Preamble += "__OBJC_RW_DLLIMPORT void _Block_object_assign(void *, const void *, const int);\n";
  Preamble += "__OBJC_RW_DLLIMPORT void _Block_object_dispose(const void *, const int);\n";
  Preamble += "__OBJC_RW_DLLIMPORT void *_NSConcreteGlobalBlock[32];\n";
  Preamble += "__OBJC_RW_DLLIMPORT void *_NSConcreteStackBlock[32];\n";
  Preamble += "#endif\n";
  Preamble += "/* LCOV_EXCL_START */\n";
  Preamble += "static inline void _Block_byref_dispose(const void *obj) {\n";
  Preamble += "    _Block_object_dispose(obj, /*BLOCK_FIELD_IS_BYREF*/8);\n";
  Preamble += "}\n";
  Preamble += "/* LCOV_EXCL_STOP */\n";
  if (LangOpts.CPlusPlus) {
    Preamble += "#include <new>\n";
    Preamble += "#define _Block_byref_cleanup\n";
  } else {
    Preamble += "#define _Block_byref_cleanup __attribute__((cleanup(_Block_byref_dispose)))\n";
  }
  Preamble += "#endif\n";
  if (LangOpts.MicrosoftExt) {
    Preamble += "#undef __OBJC_RW_DLLIMPORT\n";
    Preamble += "#undef __OBJC_RW_STATICIMPORT\n";
    Preamble += "#ifndef KEEP_ATTRIBUTES\n";  // We use this for clang tests.
    Preamble += "#define __attribute__(X)\n";
    Preamble += "#endif\n";
    Preamble += "#ifndef __weak\n";
    Preamble += "#  define __weak\n";
    Preamble += "#endif\n";
  }
  else {
    Preamble += "#ifndef __block\n";
    Preamble += "#  define __block\n";
    Preamble += "#endif\n";
    Preamble += "#ifndef __weak\n";
    Preamble += "#  define __weak\n";
    Preamble += "#endif\n";
  }
  Preamble += "# line 1 \"" + InFileName + "\"\n";
}

// We avoid calling Type::isBlockPointerType(), since it operates on the
// canonical type. We only care if the top-level type is a closure pointer.
static bool isTopLevelBlockPointerType(QualType T)
{
  return isa<BlockPointerType>(T);
}

QualType RewriteBlocks::getSimpleFunctionType(QualType result,
                                   ArrayRef<QualType> args,
                                   bool variadic) {
  FunctionProtoType::ExtProtoInfo fpi;
  fpi.Variadic = variadic;
  return Context->getFunctionType(result, args, fpi);
}

CStyleCastExpr* RewriteBlocks::NoTypeInfoCStyleCastExpr(ASTContext *Ctx, QualType Ty,
                                         CastKind Kind, Expr *E) {
  TypeSourceInfo *TInfo = Ctx->getTrivialTypeSourceInfo(Ty, SourceLocation());
#if CLANG_VERSION_MAJOR >= 12
  return CStyleCastExpr::Create(*Ctx, Ty, VK_RValue, Kind, E, nullptr,
                                FPOptionsOverride(), TInfo,
                                SourceLocation(), SourceLocation());
#else
  return CStyleCastExpr::Create(*Ctx, Ty, VK_RValue, Kind, E, 0, TInfo,
                                SourceLocation(), SourceLocation());
#endif /* CLANG_VERSION_MAJOR >= 12 */
}

/// convertBlockPointerToFunctionPointer - Converts a block-pointer type
/// to a function pointer type and upon success, returns true; false
/// otherwise.
bool RewriteBlocks::convertBlockPointerToFunctionPointer(QualType &T) {
  if (isTopLevelBlockPointerType(T)) {
    const BlockPointerType *BPT = T->getAs<BlockPointerType>();
    T = Context->getPointerType(BPT->getPointeeType());
    return true;
  }
  return false;
}

QualType RewriteBlocks::canonifyType(const QualType type) {
  if (type->isFunctionType() || type->isFunctionPointerType()
  || type->isBlockPointerType()) {
    return type;
  }
  return type.getCanonicalType();
}

QualType RewriteBlocks ::convertFunctionTypeOfBlocks(const FunctionType *FT) {
  const FunctionProtoType *FTP = dyn_cast<FunctionProtoType>(FT);
  // FTP will be null for closures that don't take arguments.
  // Generate a funky cast.
  SmallVector<QualType, 8> ArgTypes;
  QualType Res = FT->getReturnType();
  bool HasBlockType = convertBlockPointerToFunctionPointer(Res);

  if (FTP) {
    for (FunctionProtoType::param_type_iterator I = FTP->param_type_begin(),
         E = FTP->param_type_end(); I && (I != E); ++I) {
      QualType t = canonifyType(*I);
      // Make sure we convert "t (^)(...)" to "t (*)(...)".
      if (convertBlockPointerToFunctionPointer(t))
        HasBlockType = true;
      ArgTypes.push_back(t);
    }
  }
  QualType FuncType;
  // FIXME. Does this work if block takes no argument but has a return type
  // which is of block type?
  if (HasBlockType)
    FuncType = getSimpleFunctionType(Res, ArgTypes);
  else FuncType = QualType(FT, 0);
  return FuncType;
}

bool RewriteBlocks::PointerTypeTakesAnyBlockArguments(QualType QT)
{
  const FunctionProtoType *FTP;
  const PointerType *PT = QT->getAs<PointerType>();
  if (PT) {
    FTP = PT->getPointeeType()->getAs<FunctionProtoType>();
  } else {
    const BlockPointerType *BPT = QT->getAs<BlockPointerType>();
    assert(BPT && "BlockPointerTypeTakeAnyBlockArguments(): not a block pointer type");
    FTP = BPT->getPointeeType()->getAs<FunctionProtoType>();
  }
  if (FTP) {
    for (FunctionProtoType::param_type_iterator I = FTP->param_type_begin(),
         E = FTP->param_type_end(); I != E; ++I)
      if (isTopLevelBlockPointerType(*I))
        return true;
  }
  return false;
}

void RewriteBlocks::GetExtentOfArgList(const char *Name, const char *&LParen,
                                       const char *&RParen)
{
  const char *argPtr = strchr(Name, '(');
  assert((*argPtr == '(') && "Rewriter fuzzy parser confused");

  LParen = argPtr; // output the start.
  argPtr++; // skip past the left paren.
  unsigned parenCount = 1;

  while (*argPtr && parenCount) {
    switch (*argPtr) {
    case '(': parenCount++; break;
    case ')': parenCount--; break;
    default: break;
    }
    if (parenCount) argPtr++;
  }
  assert((*argPtr == ')') && "Rewriter fuzzy parser confused");
  RParen = argPtr; // output the end
}

void RewriteBlocks::GetBlockDeclRefExprs(Stmt *S) {
  for (Stmt *SubStmt : S->children())
    if (SubStmt) {
      if (BlockExpr *CBE = dyn_cast<BlockExpr>(SubStmt))
        GetBlockDeclRefExprs(CBE->getBody());
      else
        GetBlockDeclRefExprs(SubStmt);
    }
  // Handle specific things.
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(S))
    if (DRE->refersToEnclosingVariableOrCapture() ||
        HasLocalVariableExternalStorage(DRE->getDecl()))
      // FIXME: Handle enums.
      BlockDeclRefs.push_back(DRE);

  return;
}

void RewriteBlocks::GetInnerBlockDeclRefExprs(
    Stmt *S, SmallVectorImpl<DeclRefExpr *> &InnerBlockDeclRefs,
    llvm::SmallPtrSetImpl<const DeclContext *> &InnerContexts)
{
  for (Stmt *SubStmt : S->children())
    if (SubStmt) {
      if (BlockExpr *CBE = dyn_cast<BlockExpr>(SubStmt)) {
        InnerContexts.insert(cast<DeclContext>(CBE->getBlockDecl()));
        GetInnerBlockDeclRefExprs(CBE->getBody(),
                                  InnerBlockDeclRefs,
                                  InnerContexts);
      }
      else
        GetInnerBlockDeclRefExprs(SubStmt, InnerBlockDeclRefs, InnerContexts);
    }
  // Handle specific things.
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(S)) {
    if (DRE->refersToEnclosingVariableOrCapture() ||
        HasLocalVariableExternalStorage(DRE->getDecl())) {
      if (!InnerContexts.count(DRE->getDecl()->getDeclContext()))
        InnerBlockDeclRefs.push_back(DRE);
      if (VarDecl *Var = cast<VarDecl>(DRE->getDecl()))
        if (Var->isFunctionOrMethodVarDecl())
          ImportedLocalExternalDecls.insert(Var);
    }
  }

  return;
}

void RewriteBlocks::RewriteBlockPointerType(std::string& Str, QualType Type) {
  std::string TypeString(Type.getAsString(Context->getPrintingPolicy()));
  const char *argPtr = TypeString.c_str();
  if (!strchr(argPtr, '^')) {
    Str += TypeString;
    return;
  }
  while (*argPtr) {
    Str += (*argPtr == '^' ? '*' : *argPtr);
    argPtr++;
  }
}

// FIXME. Consolidate this routine with RewriteBlockPointerType.
void RewriteBlocks::RewriteBlockPointerTypeVariable(std::string& Str,
                                                    ValueDecl *VD) {
  QualType Type = VD->getType();
  std::string TypeString(Type.getAsString(Context->getPrintingPolicy()));
  const char *argPtr = TypeString.c_str();
  int paren = 0;
  while (*argPtr) {
    switch (*argPtr) {
      case '(':
        Str += *argPtr;
        paren++;
        break;
      case ')':
        Str += *argPtr;
        paren--;
        break;
      case '^':
        Str += '*';
        if (paren == 1)
          Str += VD->getNameAsString();
        break;
      default:
        Str += *argPtr;
        break;
    }
    argPtr++;
  }
}

void RewriteBlocks::RewriteBlockPointerDecl(NamedDecl *ND) {
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(ND)) {
    RewriteBlockPointerFunctionArgs(FD);
    return;
  }

  // Handle Variables and Typedefs.
  SourceLocation DeclLoc = ND->getLocation();
  QualType DeclT;

  if (DeclLoc.isMacroID()) {
    /* Macro are not supported by the rewriter */
    return;
  }

  if (VarDecl *VD = dyn_cast<VarDecl>(ND))
    DeclT = VD->getType();
  else if (TypedefNameDecl *TDD = dyn_cast<TypedefNameDecl>(ND))
    DeclT = TDD->getUnderlyingType();
  else if (FieldDecl *FD = dyn_cast<FieldDecl>(ND))
    DeclT = FD->getType();
  else
    llvm_unreachable("RewriteBlockPointerDecl(): Decl type not yet handled");

  const char *startBuf = SM->getCharacterData(DeclLoc);
  const char *endBuf = startBuf;
  // scan backward (from the decl location) for the end of the previous decl.
  while (startBuf != MainFileStart && *startBuf != '^' && *startBuf != ';') {
    assert (*startBuf);
    startBuf--;
  }
  SourceLocation Start = DeclLoc.getLocWithOffset(startBuf-endBuf);
  std::string buf;
  unsigned OrigLength=0;
  // *startBuf != '^' if we are dealing with a pointer to function that
  // may take block argument types (which will be handled below).
  if (*startBuf == '^') {
    // Replace the '^' with '*', computing a negative offset.
    buf = '*';
    startBuf++;
    OrigLength++;
  }
  while (*startBuf != ')') {
    assert (*startBuf);
    buf += *startBuf;
    startBuf++;
    OrigLength++;
  }
  buf += ')';
  OrigLength++;

  if (PointerTypeTakesAnyBlockArguments(DeclT)) {
    // Replace the '^' with '*' for arguments.
    // Replace id<P> with id/*<>*/
    DeclLoc = ND->getLocation();
    startBuf = SM->getCharacterData(DeclLoc);
    const char *argListBegin, *argListEnd;
    GetExtentOfArgList(startBuf, argListBegin, argListEnd);
    while (argListBegin < argListEnd) {
      if (*argListBegin == '^')
        buf += '*';
      else
        buf += *argListBegin;
      argListBegin++;
      OrigLength++;
    }
    buf += ')';
    OrigLength++;
  }
  /* intersec: disable warnings here we handle the carret rewriting
   * ourselves with the BLOCK_CARET macro */
  ReplaceText(Start, OrigLength, buf, false);

  return;
}

void RewriteBlocks::RewriteBlockPointerFunctionArgs(FunctionDecl *FD) {
  SourceLocation DeclLoc = FD->getLocation();
  unsigned parenCount = 0;

  // We have 1 or more arguments that have closure pointers.
  const char *startBuf = SM->getCharacterData(DeclLoc);
  const char *startArgList = strchr(startBuf, '(');

  if (!startArgList)
      return;

  parenCount++;
  // advance the location to startArgList.
  DeclLoc = DeclLoc.getLocWithOffset(startArgList-startBuf);
  assert((DeclLoc.isValid()) && "Invalid DeclLoc");

  const char *argPtr = startArgList;

  while (*argPtr++ && parenCount) {
    switch (*argPtr) {
    case '^':
      // Replace the '^' with '*'.
      /* intersec: disable warnings here we handle the carret rewriting
       * ourselves with the BLOCK_CARET macro */
      ReplaceText(DeclLoc.getLocWithOffset(argPtr-startArgList), 1, "*", false);
      break;
    case '(':
      parenCount++;
      break;
    case ')':
      parenCount--;
      break;
    }
  }
}

void RewriteBlocks::RewriteBlocksInFunctionProtoType(QualType funcType,
                                                     NamedDecl *D) {
  if (const FunctionProtoType *fproto
      = dyn_cast<FunctionProtoType>(funcType.IgnoreParens())) {
    for (FunctionProtoType::param_type_iterator I = fproto->param_type_begin(),
         E = fproto->param_type_end(); I && (I != E); ++I)
      if (isTopLevelBlockPointerType(*I)) {
        // All the args are checked/rewritten. Don't call twice!
        RewriteBlockPointerDecl(D);
        break;
      }
  }
}

void RewriteBlocks::CheckFunctionPointerDecl(QualType funcType, NamedDecl *ND) {
  const PointerType *PT = funcType->getAs<PointerType>();
  if (PT && PointerTypeTakesAnyBlockArguments(funcType))
    RewriteBlocksInFunctionProtoType(PT->getPointeeType(), ND);
}

void RewriteBlocks::RewriteBlockLiteralFunctionDecl(FunctionDecl *FD) {
  SourceLocation FunLocStart = FD->getTypeSpecStartLoc();
  const FunctionType *funcType = FD->getType()->getAs<FunctionType>();
  const FunctionProtoType *proto = dyn_cast<FunctionProtoType>(funcType);
  if (!proto)
    return;
  QualType Type = proto->getReturnType();
  std::string FdStr = Type.getAsString(Context->getPrintingPolicy());
  FdStr += " ";
  FdStr += FD->getName();
  FdStr +=  "(";
  unsigned numArgs = proto->getNumParams();
  for (unsigned i = 0; i < numArgs; i++) {
    QualType ArgType = proto->getParamType(i);
    RewriteBlockPointerType(FdStr, ArgType);
    if (i+1 < numArgs)
      FdStr += ", ";
  }
  if (FD->isVariadic()) {
    if (numArgs != 0) {
      FdStr += ", ";
    }
    FdStr += "...";
  } else if (numArgs == 0) {
    FdStr += "void";
  }
  FdStr += ");\n";
  InsertText(FunLocStart, FdStr);
  CurFunctionDeclToDeclareForBlock = 0;
}

void RewriteBlocks::CollectBlockDeclRefInfo(BlockExpr *Exp) {
  // Add initializers for any closure decl refs.
  GetBlockDeclRefExprs(Exp->getBody());
  if (BlockDeclRefs.size()) {
    // Unique all "by copy" declarations.
    for (unsigned i = 0; i < BlockDeclRefs.size(); i++)
      if (!BlockDeclRefs[i]->getDecl()->hasAttr<BlocksAttr>()) {
        if (!BlockByCopyDeclsPtrSet.count(BlockDeclRefs[i]->getDecl())) {
          BlockByCopyDeclsPtrSet.insert(BlockDeclRefs[i]->getDecl());
          BlockByCopyDecls.push_back(BlockDeclRefs[i]->getDecl());
        }
      }
    // Unique all "by ref" declarations.
    for (unsigned i = 0; i < BlockDeclRefs.size(); i++)
      if (BlockDeclRefs[i]->getDecl()->hasAttr<BlocksAttr>()) {
        if (!BlockByRefDeclsPtrSet.count(BlockDeclRefs[i]->getDecl())) {
          BlockByRefDeclsPtrSet.insert(BlockDeclRefs[i]->getDecl());
          BlockByRefDecls.push_back(BlockDeclRefs[i]->getDecl());
        }
      }
    // Find any imported blocks...they will need special attention.
    for (unsigned i = 0; i < BlockDeclRefs.size(); i++)
      if (BlockDeclRefs[i]->getDecl()->hasAttr<BlocksAttr>() ||
          BlockDeclRefs[i]->getType()->isBlockPointerType())
        ImportedBlockDecls.insert(BlockDeclRefs[i]->getDecl());
  }
}

void RewriteBlocks::RewriteByRefString(std::string &ResultStr,
                                     const std::string &Name,
                                     ValueDecl *VD, bool def) {
  assert(BlockByRefDeclNo.count(VD) &&
         "RewriteByRefString: ByRef decl missing");
    ResultStr += "struct ";
  ResultStr += "__Block_byref_" + Name +
    "_" + utostr(BlockByRefDeclNo[VD]) ;
}

// We need to return the rewritten expression to handle cases where the
// BlockDeclRefExpr is embedded in another expression being rewritten.
// For example:
//
// int main() {
//    __block Foo *f;
//    __block int i;
//
//    void (^myblock)() = ^() {
//        [f test]; // f is a BlockDeclRefExpr embedded in a message (which is being rewritten).
//        i = 77;
//    };
//}
Stmt *RewriteBlocks::RewriteBlockDeclRefExpr(DeclRefExpr *DeclRefExp) {
  // Rewrite the byref variable into BYREFVAR->__forwarding->BYREFVAR
  // for each DeclRefExp where BYREFVAR is name of the variable.
  ValueDecl *VD = DeclRefExp->getDecl();
  bool isArrow = DeclRefExp->refersToEnclosingVariableOrCapture() ||
                 HasLocalVariableExternalStorage(DeclRefExp->getDecl());

  FieldDecl *FD = FieldDecl::Create(*Context, 0, SourceLocation(),
                                    SourceLocation(),
                                    &Context->Idents.get("__forwarding"),
                                    Context->VoidPtrTy, 0,
                                    /*BitWidth=*/0,
                                    /*Mutable=*/true,
                                    ICIS_NoInit);
  MemberExpr *ME =
      MemberExpr::CreateImplicit(*Context, DeclRefExp, isArrow, FD,
                                 FD->getType(), VK_LValue, OK_Ordinary);

  StringRef Name = VD->getName();
  FD = FieldDecl::Create(*Context, 0, SourceLocation(), SourceLocation(),
                         &Context->Idents.get(Name),
                         Context->VoidPtrTy, 0,
                         /*BitWidth=*/0, /*Mutable=*/true,
                         ICIS_NoInit);
  ME = MemberExpr::CreateImplicit(*Context, ME, true, FD, DeclRefExp->getType(),
                                  VK_LValue, OK_Ordinary);

  // Need parens to enforce precedence.
  ParenExpr *PE = new (Context) ParenExpr(DeclRefExp->getExprLoc(),
                                          DeclRefExp->getExprLoc(),
                                          ME);
  ReplaceStmt(DeclRefExp, PE);
  return PE;
}

// Rewrites the imported local variable V with external storage
// (static, extern, etc.) as *V
//
Stmt *RewriteBlocks::RewriteLocalVariableExternalStorage(DeclRefExpr *DRE) {
  ValueDecl *VD = DRE->getDecl();
  if (VarDecl *Var = dyn_cast<VarDecl>(VD))
    if (!ImportedLocalExternalDecls.count(Var))
      return DRE;

#if CLANG_VERSION_MAJOR >= 11
  Expr *Exp = UnaryOperator::Create(
      const_cast<ASTContext &>(*Context), DRE, UO_Deref, DRE->getType(),
      VK_LValue, OK_Ordinary, DRE->getLocation(), false, FPOptionsOverride());
#else
  Expr *Exp = new (Context) UnaryOperator(DRE, UO_Deref, DRE->getType(),
                                          VK_LValue, OK_Ordinary,
                                          DRE->getLocation(), false);
#endif /* CLANG_VERSION_MAJOR >= 11 */

  // Need parens to enforce precedence.
  ParenExpr *PE = new (Context) ParenExpr(SourceLocation(), SourceLocation(),
                                          Exp);
  ReplaceStmt(DRE, PE);
  return PE;
}

/// RewriteByRefVar - For each __block typex ND variable this routine transforms
/// the declaration into:
/// struct __Block_byref_ND {
/// void *__isa;                  // NULL for everything except __weak pointers
/// struct __Block_byref_ND *__forwarding;
/// int32_t __flags;
/// int32_t __size;
/// void *__Block_byref_id_object_copy; // If variable is __block ObjC object
/// void *__Block_byref_id_object_dispose; // If variable is __block ObjC object
/// typex ND;
/// };
///
/// It then replaces declaration of ND variable with:
/// struct __Block_byref_ND ND = {__isa=0B, __forwarding=&ND, __flags=some_flag,
///                               __size=sizeof(struct __Block_byref_ND),
///                               ND=initializer-if-any};
///
///
void RewriteBlocks::RewriteByRefVar(VarDecl *ND) {
  // Insert declaration for the function in which block literal is
  // used.
  if (CurFunctionDeclToDeclareForBlock
      && CurFunctionDeclToDeclareForBlock->getStorageClass() == SC_Static)
    RewriteBlockLiteralFunctionDecl(CurFunctionDeclToDeclareForBlock);
  int flag = 0;
  int isa = 0;
  SourceLocation DeclLoc = ND->getTypeSpecStartLoc();
  if (DeclLoc.isInvalid())
    // If type location is missing, it is because of missing type (a warning).
    // Use variable's location which is good for this case.
    DeclLoc = ND->getLocation();
  const char *startBuf = SM->getCharacterData(DeclLoc);
  SourceLocation X = ND->getEndLoc();
  X = SM->getExpansionLoc(X);
  const char *endBuf = SM->getCharacterData(X);
  std::string Name(ND->getNameAsString());
  std::string ByrefType;
  RewriteByRefString(ByrefType, Name, ND, true);
  ByrefType += " {\n";
  ByrefType += "  void *__isa;\n";
  RewriteByRefString(ByrefType, Name, ND);
  ByrefType += " *__forwarding;\n";
  ByrefType += " int __flags;\n";
  ByrefType += " int __size;\n";
  if (LangOpts.CPlusPlus) {
    ByrefType += "  ~__Block_byref_" + Name + "_" + utostr(BlockByRefDeclNo[ND])
        + "() { _Block_byref_dispose(this); }\n";
  }
  // Add void *__Block_byref_id_object_copy;
  // void *__Block_byref_id_object_dispose; if needed.
  QualType Ty = canonifyType(ND->getType());
  bool HasCopyAndDispose = Context->BlockRequiresCopying(Ty, ND);
  if (HasCopyAndDispose) {
    ByrefType += " void (*__Block_byref_id_object_copy)(void*, void*);\n";
    ByrefType += " void (*__Block_byref_id_object_dispose)(void*);\n";
  }

  QualType T = Ty;
  (void)convertBlockPointerToFunctionPointer(T);
  T.getAsStringInternal(Name, Context->getPrintingPolicy());

  ByrefType += " " + Name + ";\n";
  ByrefType += "};\n";
  // Insert this type in global scope. It is needed by helper function.
  SourceLocation FunLocStart;
  FunLocStart = CurFunctionDef->getTypeSpecStartLoc();
  InsertText(FunLocStart, ByrefType);

  if (HasCopyAndDispose) {
    flag = BLOCK_BYREF_CALLER;
    QualType Ty = ND->getType();
    // FIXME. Handle __weak variable (BLOCK_FIELD_IS_WEAK) as well.
    if (Ty->isBlockPointerType())
      flag |= BLOCK_FIELD_IS_BLOCK;
    else
      flag |= BLOCK_FIELD_IS_OBJECT;
    std::string HF = SynthesizeByrefCopyDestroyHelper(ND, flag);
    if (!HF.empty())
      InsertText(FunLocStart, HF);
  }

  // struct __Block_byref_ND ND =
  // {0, &ND, some_flag, __size=sizeof(struct __Block_byref_ND),
  //  initializer-if-any};
  bool hasInit = (ND->getInit() != 0);
  unsigned flags = 0;
  if (HasCopyAndDispose)
    flags |= BLOCK_HAS_COPY_DISPOSE;
  Name = ND->getNameAsString();
  ByrefType.clear();
  RewriteByRefString(ByrefType, Name, ND);
  std::string ForwardingCastType("(");
  ForwardingCastType += ByrefType + " *)";
  if (!hasInit) {
    ByrefType += " _Block_byref_cleanup " + Name + " = {(void*)";
    ByrefType += utostr(isa);
    ByrefType += "," +  ForwardingCastType + "&" + Name + ", ";
    ByrefType += utostr(flags);
    ByrefType += ", ";
    ByrefType += "sizeof(";
    RewriteByRefString(ByrefType, Name, ND);
    ByrefType += ")";
    if (HasCopyAndDispose) {
      ByrefType += ", __Block_byref_id_object_copy_";
      ByrefType += utostr(flag);
      ByrefType += ", __Block_byref_id_object_dispose_";
      ByrefType += utostr(flag);
    }
    ByrefType += "};\n";
    unsigned nameSize = Name.size();
    // for block or function pointer declaration. Name is aleady
    // part of the declaration.
    if (Ty->isBlockPointerType() || Ty->isFunctionPointerType())
      nameSize = 1;
    ReplaceText(DeclLoc, endBuf-startBuf+nameSize, ByrefType, true);
  }
  else {
    SourceLocation startLoc;
    Expr *E = ND->getInit();
    if (const CStyleCastExpr *ECE = dyn_cast<CStyleCastExpr>(E))
      startLoc = ECE->getLParenLoc();
    else
      startLoc = E->getBeginLoc();
    startLoc = SM->getExpansionLoc(startLoc);
    endBuf = SM->getCharacterData(startLoc);
    ByrefType += " _Block_byref_cleanup " + Name;
    ByrefType += " = {(void*)";
    ByrefType += utostr(isa);
    ByrefType += "," +  ForwardingCastType + "&" + Name + ", ";
    ByrefType += utostr(flags);
    ByrefType += ", ";
    ByrefType += "sizeof(";
    RewriteByRefString(ByrefType, Name, ND);
    ByrefType += "), ";
    if (HasCopyAndDispose) {
      ByrefType += "__Block_byref_id_object_copy_";
      ByrefType += utostr(flag);
      ByrefType += ", __Block_byref_id_object_dispose_";
      ByrefType += utostr(flag);
      ByrefType += ", ";
    }
    ReplaceText(DeclLoc, endBuf-startBuf, ByrefType, true);

    // Complete the newly synthesized compound expression by inserting a right
    // curly brace before the end of the declaration.
    // FIXME: This approach avoids rewriting the initializer expression. It
    // also assumes there is only one declarator. For example, the following
    // isn't currently supported by this routine (in general):
    //
    // double __block BYREFVAR = 1.34, BYREFVAR2 = 1.37;
    //
    const char *startInitializerBuf = SM->getCharacterData(startLoc);
    const char *semiBuf = strchr(startInitializerBuf, ';');
    assert((*semiBuf == ';') && "RewriteByRefVar: can't find ';'");
    SourceLocation semiLoc =
      startLoc.getLocWithOffset(semiBuf-startInitializerBuf);

    InsertText(semiLoc, "}");
  }
  return;
}

/// SynthesizeByrefCopyDestroyHelper - This routine synthesizes:
/// void __Block_byref_id_object_copy(struct Block_byref_id_object *dst,
///                    struct Block_byref_id_object *src) {
///  _Block_object_assign (&_dest->object, _src->object,
///                        BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_OBJECT
///                        [|BLOCK_FIELD_IS_WEAK]) // object
///  _Block_object_assign(&_dest->object, _src->object,
///                       BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_BLOCK
///                       [|BLOCK_FIELD_IS_WEAK]) // block
/// }
/// And:
/// void __Block_byref_id_object_dispose(struct Block_byref_id_object *_src) {
///  _Block_object_dispose(_src->object,
///                        BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_OBJECT
///                        [|BLOCK_FIELD_IS_WEAK]) // object
///  _Block_object_dispose(_src->object,
///                         BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_BLOCK
///                         [|BLOCK_FIELD_IS_WEAK]) // block
/// }

std::string RewriteBlocks::SynthesizeByrefCopyDestroyHelper(VarDecl *VD,
                                                          int flag) {
  std::string S;
  if (CopyDestroyCache.count(flag))
    return S;
  CopyDestroyCache.insert(flag);
  S = "static void __Block_byref_id_object_copy_";
  S += utostr(flag);
  S += "(void *dst, void *src) {\n";

  // offset into the object pointer is computed as:
  // void * + void* + int + int + void* + void *
  unsigned IntSize =
  static_cast<unsigned>(Context->getTypeSize(Context->IntTy));
  unsigned VoidPtrSize =
  static_cast<unsigned>(Context->getTypeSize(Context->VoidPtrTy));

  unsigned offset = (VoidPtrSize*4 + IntSize + IntSize)/Context->getCharWidth();
  S += " _Block_object_assign((char*)dst + ";
  S += utostr(offset);
  S += ", *(void * *) ((char*)src + ";
  S += utostr(offset);
  S += "), ";
  S += utostr(flag);
  S += ");\n}\n";

  S += "static void __Block_byref_id_object_dispose_";
  S += utostr(flag);
  S += "(void *src) {\n";
  S += " _Block_object_dispose(*(void * *) ((char*)src + ";
  S += utostr(offset);
  S += "), ";
  S += utostr(flag);
  S += ");\n}\n";
  return S;
}

void RewriteBlocks::RewriteCastExpr(CStyleCastExpr *CE) {
  SourceLocation LocStart = CE->getLParenLoc();
  SourceLocation LocEnd = CE->getRParenLoc();

  // Need to avoid trying to rewrite synthesized casts.
  if (LocStart.isInvalid())
    return;
  // Need to avoid trying to rewrite casts contained in macros.
  if (!Rewriter::isRewritable(LocStart) || !Rewriter::isRewritable(LocEnd))
    return;

  const char *startBuf = SM->getCharacterData(LocStart);
  const char *endBuf = SM->getCharacterData(LocEnd);
  QualType QT = CE->getType();
  const Type* TypePtr = QT->getAs<Type>();
  if (isa<TypeOfExprType>(TypePtr)) {
    const TypeOfExprType *TypeOfExprTypePtr = cast<TypeOfExprType>(TypePtr);
    QT = TypeOfExprTypePtr->getUnderlyingExpr()->getType();
    std::string TypeAsString = "(";
    RewriteBlockPointerType(TypeAsString, QT);
    TypeAsString += ")";
    ReplaceText(LocStart, endBuf-startBuf+1, TypeAsString, true);
    return;
  }
  // advance the location to startArgList.
  const char *argPtr = startBuf;

  while (*argPtr++ && (argPtr < endBuf)) {
    switch (*argPtr) {
    case '^':
      // Replace the '^' with '*'.
      LocStart = LocStart.getLocWithOffset(argPtr-startBuf);
      ReplaceText(LocStart, 1, "*", true);
      break;
    }
  }
  return;
}

void RewriteBlocks::RewriteRecordBody(RecordDecl *RD) {
  for (RecordDecl::field_iterator i = RD->field_begin(),
                                  e = RD->field_end(); i != e; ++i) {
    FieldDecl *FD = *i;
    if (isTopLevelBlockPointerType(FD->getType()))
      RewriteBlockPointerDecl(FD);
  }
}

std::string RewriteBlocks::SynthesizeBlockFunc(BlockExpr *CE, int i,
                                                   StringRef funcName,
                                                   std::string Tag) {
  const FunctionType *AFT = CE->getFunctionType();
  QualType RT = canonifyType(AFT->getReturnType());
  std::string StructRef = "struct " + Tag;
  std::string S = "static " + RT.getAsString(Context->getPrintingPolicy()) + " __" +
                  funcName.str() + "_" + "block_func_" + utostr(i);

  BlockDecl *BD = CE->getBlockDecl();

  if (isa<FunctionNoProtoType>(AFT)) {
    // No user-supplied arguments. Still need to pass in a pointer to the
    // block (to reference imported block decl refs).
    S += "(" + StructRef + " *__cself)";
  } else if (BD->param_empty()) {
    S += "(" + StructRef + " *__cself)";
  } else {
    const FunctionProtoType *FT = cast<FunctionProtoType>(AFT);
    assert(FT && "SynthesizeBlockFunc: No function proto");
    S += '(';
    // first add the implicit argument.
    S += StructRef + " *__cself, ";
    std::string ParamStr;
    for (BlockDecl::param_iterator AI = BD->param_begin(),
         E = BD->param_end(); AI != E; ++AI) {
      if (AI != BD->param_begin()) S += ", ";
      ParamStr = (*AI)->getNameAsString();
      QualType QT = (*AI)->getType();
      (void)convertBlockPointerToFunctionPointer(QT);
      QT.getAsStringInternal(ParamStr, Context->getPrintingPolicy());
      S += ParamStr;
    }
    if (FT->isVariadic()) {
      if (!BD->param_empty()) S += ", ";
      S += "...";
    }
    S += ')';
  }
  S += " {\n";

  // Create local declarations to avoid rewriting all closure decl ref exprs.
  // First, emit a declaration for all "by ref" decls.
  for (SmallVector<ValueDecl*,8>::iterator I = BlockByRefDecls.begin(),
       E = BlockByRefDecls.end(); I != E; ++I) {
    S += "  ";
    std::string Name = (*I)->getNameAsString();
    std::string TypeString;
    RewriteByRefString(TypeString, Name, (*I));
    TypeString += " *";
    Name = TypeString + Name;
    S += Name + " = __cself->" + (*I)->getNameAsString() + "; // bound by ref\n";
  }
  // Next, emit a declaration for all "by copy" declarations.
  for (SmallVector<ValueDecl*,8>::iterator I = BlockByCopyDecls.begin(),
       E = BlockByCopyDecls.end(); I != E; ++I) {
    S += "  ";
    // Handle nested closure invocation. For example:
    //
    //   void (^myImportedClosure)(void);
    //   myImportedClosure  = ^(void) { setGlobalInt(x + y); };
    //
    //   void (^anotherClosure)(void);
    //   anotherClosure = ^(void) {
    //     myImportedClosure(); // import and invoke the closure
    //   };
    //
    if (isTopLevelBlockPointerType((*I)->getType())) {
      RewriteBlockPointerTypeVariable(S, (*I));
      S += " = (";
      RewriteBlockPointerType(S, (*I)->getType());
      S += ")";
      S += "__cself->" + (*I)->getNameAsString() + "; // bound by copy\n";
    }
    else {
      std::string Name = (*I)->getNameAsString();
      QualType QT = (*I)->getType();
      if (HasLocalVariableExternalStorage(*I))
        QT = Context->getPointerType(QT);
      QT = canonifyType(QT);
      QT.getAsStringInternal(Name, Context->getPrintingPolicy());
      S += Name + " = __cself->" +
                              (*I)->getNameAsString() + "; // bound by copy\n";
    }
  }
  S += RewrittenBlockExprs[CE];
  S += "\n";
  return S;
}

std::string RewriteBlocks::SynthesizeBlockHelperFuncs(BlockExpr *CE, int i,
                                                      StringRef funcName,
                                                      std::string Tag) {
  std::string StructRef = "struct " + Tag;
  std::string S = "static void __";

  S += funcName;
  S += "_block_copy_" + utostr(i);
  S += "(" + StructRef;
  S += "*dst, " + StructRef;
  S += "*src) {";
  for (llvm::SmallPtrSet<ValueDecl*,8>::iterator I = ImportedBlockDecls.begin(),
      E = ImportedBlockDecls.end(); I != E; ++I) {
    ValueDecl *VD = (*I);
    S += "_Block_object_assign((void*)&dst->";
    S += (*I)->getNameAsString();
    S += ", (void*)src->";
    S += (*I)->getNameAsString();
    if (BlockByRefDeclsPtrSet.count((*I)))
      S += ", " + utostr(BLOCK_FIELD_IS_BYREF) + "/*BLOCK_FIELD_IS_BYREF*/);";
    else if (VD->getType()->isBlockPointerType())
      S += ", " + utostr(BLOCK_FIELD_IS_BLOCK) + "/*BLOCK_FIELD_IS_BLOCK*/);";
    else
      S += ", " + utostr(BLOCK_FIELD_IS_BLOCK) + "/*BLOCK_FIELD_IS_BLOCK*/);";
  }
  S += "}\n";

  S += "\nstatic void __";
  S += funcName;
  S += "_block_dispose_" + utostr(i);
  S += "(" + StructRef;
  S += "*src) {";
  for (llvm::SmallPtrSet<ValueDecl*,8>::iterator I = ImportedBlockDecls.begin(),
      E = ImportedBlockDecls.end(); I != E; ++I) {
    ValueDecl *VD = (*I);
    S += "_Block_object_dispose((void*)src->";
    S += (*I)->getNameAsString();
    if (BlockByRefDeclsPtrSet.count((*I)))
      S += ", " + utostr(BLOCK_FIELD_IS_BYREF) + "/*BLOCK_FIELD_IS_BYREF*/);";
    else if (VD->getType()->isBlockPointerType())
      S += ", " + utostr(BLOCK_FIELD_IS_BLOCK) + "/*BLOCK_FIELD_IS_BLOCK*/);";
    else
      S += ", " + utostr(BLOCK_FIELD_IS_BLOCK) + "/*BLOCK_FIELD_IS_BLOCK*/);";
  }
  S += "}\n";
  return S;
}

std::string RewriteBlocks::SynthesizeBlockImpl(BlockExpr *CE, std::string Tag,
                                             std::string Desc) {
  std::string S = "\nstruct " + Tag;
  std::string Constructor = "  " + Tag;
  std::string cConstructor = "#define " + Tag;

  S += " {\n  struct __block_impl impl;\n";
  S += "  struct " + Desc;
  S += "* Desc;\n";

  Constructor += "(void *fp, "; // Invoke function pointer.
  Constructor += "struct " + Desc; // Descriptor pointer.
  Constructor += " *desc";
  cConstructor += "(__blk_fp, __blk_desc";

  if (BlockDeclRefs.size()) {
    // Output all "by copy" declarations.
    for (SmallVector<ValueDecl*,8>::iterator I = BlockByCopyDecls.begin(),
         E = BlockByCopyDecls.end(); I != E; ++I) {
      S += "  ";
      std::string FieldName = (*I)->getNameAsString();
      std::string ArgName = "_" + FieldName;
      // Handle nested closure invocation. For example:
      //
      //   void (^myImportedBlock)(void);
      //   myImportedBlock  = ^(void) { setGlobalInt(x + y); };
      //
      //   void (^anotherBlock)(void);
      //   anotherBlock = ^(void) {
      //     myImportedBlock(); // import and invoke the closure
      //   };
      //
      cConstructor += ", " + ArgName;
      if (isTopLevelBlockPointerType((*I)->getType())) {
        S += "struct __block_impl *";
        Constructor += ", void *" + ArgName;
      } else {
        QualType QT = (*I)->getType();
        if (HasLocalVariableExternalStorage(*I))
          QT = Context->getPointerType(QT);
        QT = canonifyType(QT);
        QT.getAsStringInternal(FieldName, Context->getPrintingPolicy());
        QT.getAsStringInternal(ArgName, Context->getPrintingPolicy());
        Constructor += ", " + ArgName;
      }
      S += FieldName + ";\n";
    }
    // Output all "by ref" declarations.
    for (SmallVector<ValueDecl*,8>::iterator I = BlockByRefDecls.begin(),
         E = BlockByRefDecls.end(); I != E; ++I) {
      S += "  ";
      std::string FieldName = (*I)->getNameAsString();
      std::string ArgName = "_" + FieldName;
      {
        std::string TypeString;
        RewriteByRefString(TypeString, FieldName, (*I));
        TypeString += " *";
        FieldName = TypeString + FieldName;
        cConstructor += ", " + ArgName;
        ArgName = TypeString + ArgName;
        Constructor += ", " + ArgName;
      }
      S += FieldName + "; // by ref\n";
    }
    // Finish writing the constructor.
    Constructor += ", int flags=0)";
    Constructor += " : impl(";
    Constructor += GlobalVarDecl ? "&_NSConcreteGlobalBlock, "
                                 : "&_NSConcreteStackBlock, ";
    Constructor += "flags, fp), Desc(desc)";
    cConstructor += ", __blk_flags) \\\n";
    cConstructor += "  { \\\n";
    // Initialize all "by copy" arguments.
    for (SmallVector<ValueDecl*,8>::iterator I = BlockByCopyDecls.begin(),
         E = BlockByCopyDecls.end(); I != E; ++I) {
      std::string Name = (*I)->getNameAsString();
        Constructor += ", ";
        cConstructor += "    ." + Name + " = ";
        if (isTopLevelBlockPointerType((*I)->getType())) {
          Constructor += Name + "((struct __block_impl *)_" + Name + ")";
          cConstructor += "((struct __block_impl *)(_" + Name + ")), \\\n";
        } else {
          Constructor += Name + "(_" + Name + ")";
          cConstructor += "(_" + Name + "), \\\n";
        }
    }
    // Initialize all "by ref" arguments.
    for (SmallVector<ValueDecl*,8>::iterator I = BlockByRefDecls.begin(),
         E = BlockByRefDecls.end(); I != E; ++I) {
      std::string Name = (*I)->getNameAsString();
      Constructor += ", ";
      cConstructor += "    ." + Name + " = ";
      Constructor += Name + "(_" + Name + "->__forwarding)";
      cConstructor += "((_" + Name + ")->__forwarding), \\\n";
    }
  } else {
    // Finish writing the constructor.
    Constructor += ", int flags=0)\n";
    Constructor += " : impl(";
    Constructor += GlobalVarDecl ? "&_NSConcreteGlobalBlock, "
                                 : "&_NSConcreteStackBlock, ";
    Constructor += "flags, fp), Desc(desc)";
    cConstructor += ", __blk_flags) \\\n";
    cConstructor += "  { \\\n";
  }

  Constructor  += " { }\n";
  cConstructor += "    .impl = { \\\n";
  if (GlobalVarDecl) {
    cConstructor += "      .isa = &_NSConcreteGlobalBlock, \\\n";
  } else {
    cConstructor += "      .isa = &_NSConcreteStackBlock, \\\n";
  }
  cConstructor +=
    "      .Flags = (__blk_flags), \\\n"
    "      .FuncPtr = (__blk_fp), \\\n"
    "    }, \\\n"
    "    .Desc = (__blk_desc), \\\n"
    "  }\n";

  if (LangOpts.CPlusPlus) {
    S += Constructor;
    S += "  " + Tag + "() : impl(NULL, 0, NULL) { }\n";
    if (!GlobalVarDecl) {
        S += "#define " + Tag + "__INST(...)  (new((void *)&" + Tag + "__VAR) " + Tag + "(__VA_ARGS__))\n";
    } else {
        S += "#define " + Tag + "__INST(...)  (&" + Tag + "__VAR)\n";
    }
  } else {
    S += cConstructor;
    if (!GlobalVarDecl) {
        S += "#define " + Tag + "__INST(...)  ({ \\\n";
        S += "    memcpy(&" + Tag + "__VAR, &(struct " + Tag + ")" + Tag + "(__VA_ARGS__), sizeof(" + Tag + "__VAR)); \\\n";
        S += "    &" + Tag + "__VAR; \\\n";
        S += "  })\n";
    } else {
        S += "#define " + Tag + "__INST(...)  (&" + Tag + "__VAR)\n";
    }
  }
  S += "};\n";
  return S;
}

std::string RewriteBlocks::SynthesizeBlockDescriptor(std::string DescTag,
                                                   std::string ImplTag, int i,
                                                   StringRef FunName,
                                                   unsigned hasCopy) {
  std::string S = "\nstatic struct " + DescTag;

  S += " {\n  unsigned long reserved;\n";
  S += "  unsigned long Block_size;\n";
  if (hasCopy) {
    S += "  void (*copy)(struct ";
    S += ImplTag; S += "*, struct ";
    S += ImplTag; S += "*);\n";

    S += "  void (*dispose)(struct ";
    S += ImplTag; S += "*);\n";
  }
  S += "} ";

  S += DescTag + "_DATA = { 0, sizeof(struct ";
  S += ImplTag + ")";
  if (hasCopy) {
    S += ", __" + FunName.str() + "_block_copy_" + utostr(i);
    S += ", __" + FunName.str() + "_block_dispose_" + utostr(i);
  }
  S += "};\n";
  return S;
}

void RewriteBlocks::SynthesizeBlockLiterals(SourceLocation FunLocStart,
                                            StringRef FunName)
{
  // Insert declaration for the function in which block literal is used.
  if (CurFunctionDeclToDeclareForBlock && !Blocks.empty()
      && CurFunctionDeclToDeclareForBlock->getStorageClass() == SC_Static)
    RewriteBlockLiteralFunctionDecl(CurFunctionDeclToDeclareForBlock);
  bool RewriteSC = (GlobalVarDecl &&
                    !Blocks.empty() &&
                    GlobalVarDecl->getStorageClass() == SC_Static &&
                    GlobalVarDecl->getType().getCVRQualifiers());
  if (RewriteSC) {
    std::string SC(" void __");
    SC += GlobalVarDecl->getNameAsString();
    SC += "() {}";
    InsertText(FunLocStart, SC);
  }

  // Insert closures that were part of the function.
  for (unsigned i = 0, count=0; i < Blocks.size(); i++) {
    CollectBlockDeclRefInfo(Blocks[i]);
    // Need to copy-in the inner copied-in variables not actually used in this
    // block.
    for (int j = 0; j < InnerDeclRefsCount[i]; j++) {
      DeclRefExpr *Exp = InnerDeclRefs[count++];
      ValueDecl *VD = Exp->getDecl();
      BlockDeclRefs.push_back(Exp);
      if (!VD->hasAttr<BlocksAttr>() && !BlockByCopyDeclsPtrSet.count(VD)) {
        BlockByCopyDeclsPtrSet.insert(VD);
        BlockByCopyDecls.push_back(VD);
      }
      if (VD->hasAttr<BlocksAttr>() && !BlockByRefDeclsPtrSet.count(VD)) {
        BlockByRefDeclsPtrSet.insert(VD);
        BlockByRefDecls.push_back(VD);
      }
      // imported objects in the inner blocks not used in the outer
      // blocks must be copied/disposed in the outer block as well.
      if (VD->hasAttr<BlocksAttr>() ||
          VD->getType()->isBlockPointerType())
        ImportedBlockDecls.insert(VD);
    }

    std::string ImplTag = "__" + FunName.str() + "_block_impl_" + utostr(i);
    std::string DescTag = "__" + FunName.str() + "_block_desc_" + utostr(i);
    std::string FuncTag = "__" + FunName.str() + "_block_func_" + utostr(i);

    std::string CI = SynthesizeBlockImpl(Blocks[i], ImplTag, DescTag);

    InsertText(FunLocStart, CI);

    std::string CF = SynthesizeBlockFunc(Blocks[i], i, FunName, ImplTag);

    InsertText(FunLocStart, CF);

    if (ImportedBlockDecls.size()) {
      std::string HF = SynthesizeBlockHelperFuncs(Blocks[i], i, FunName, ImplTag);
      InsertText(FunLocStart, HF);
    }
    std::string BD = SynthesizeBlockDescriptor(DescTag, ImplTag, i, FunName,
                                               ImportedBlockDecls.size() > 0);
    InsertText(FunLocStart, BD);
    PutSharpLine(FunLocStart);

    if (GlobalVarDecl) {
      std::string S;
      std::string Args = "(void *)" + FuncTag + ", &" + DescTag + "_DATA, 0";
      if (LangOpts.CPlusPlus) {
        S = "static struct " + ImplTag + " " + ImplTag + "__VAR(" + Args + ");\n";
      } else {
        S = "static struct " + ImplTag + " " + ImplTag + "__VAR = " + ImplTag + "(" + Args + ");\n";
      }
      InsertText(FunLocStart, S);
    }

    BlockDeclRefs.clear();
    BlockByRefDecls.clear();
    BlockByRefDeclsPtrSet.clear();
    BlockByCopyDecls.clear();
    BlockByCopyDeclsPtrSet.clear();
    ImportedBlockDecls.clear();
  }
  if (RewriteSC) {
    // Must insert any 'const/volatile/static here. Since it has been
    // removed as result of rewriting of block literals.
    std::string SC;
    if (GlobalVarDecl->getStorageClass() == SC_Static)
      SC = "static ";
    if (GlobalVarDecl->getType().isConstQualified())
      SC += "const ";
    if (GlobalVarDecl->getType().isVolatileQualified())
      SC += "volatile ";
    if (GlobalVarDecl->getType().isRestrictQualified())
      SC += "restrict ";
    InsertText(FunLocStart, SC);
  }

  Blocks.clear();
  InnerDeclRefsCount.clear();
  InnerDeclRefs.clear();
  RewrittenBlockExprs.clear();
}

FunctionDecl *RewriteBlocks::SynthBlockInitFunctionDecl(StringRef name) {
  IdentifierInfo *ID = &Context->Idents.get(name);
  QualType FType = Context->getFunctionNoProtoType(Context->VoidPtrTy);
  return FunctionDecl::Create(*Context, TUDecl, SourceLocation(),
                              SourceLocation(), ID, FType, 0, SC_Extern,
                              false, false);
}

Stmt *RewriteBlocks::SynthBlockInitExpr(BlockExpr *Exp,
                                        const SmallVector<DeclRefExpr *, 8> &InnerBlockDeclRefs,
                                        CompoundStmt *CS) {
  const BlockDecl *block = Exp->getBlockDecl();
  Blocks.push_back(Exp);

  CollectBlockDeclRefInfo(Exp);

  // Add inner imported variables now used in current block.
 int countOfInnerDecls = 0;
  if (!InnerBlockDeclRefs.empty()) {
    for (unsigned i = 0; i < InnerBlockDeclRefs.size(); i++) {
      DeclRefExpr *Exp = InnerBlockDeclRefs[i];
      ValueDecl *VD = Exp->getDecl();
      if (!VD->hasAttr<BlocksAttr>() && !BlockByCopyDeclsPtrSet.count(VD)) {
      // We need to save the copied-in variables in nested
      // blocks because it is needed at the end for some of the API generations.
      // See SynthesizeBlockLiterals routine.
        InnerDeclRefs.push_back(Exp); countOfInnerDecls++;
        BlockDeclRefs.push_back(Exp);
        BlockByCopyDeclsPtrSet.insert(VD);
        BlockByCopyDecls.push_back(VD);
      }
      if (VD->hasAttr<BlocksAttr>() && !BlockByRefDeclsPtrSet.count(VD)) {
        InnerDeclRefs.push_back(Exp); countOfInnerDecls++;
        BlockDeclRefs.push_back(Exp);
        BlockByRefDeclsPtrSet.insert(VD);
        BlockByRefDecls.push_back(VD);
      }
    }
    // Find any imported blocks...they will need special attention.
    for (unsigned i = 0; i < InnerBlockDeclRefs.size(); i++)
      if (InnerBlockDeclRefs[i]->getDecl()->hasAttr<BlocksAttr>() ||
          InnerBlockDeclRefs[i]->getType()->isBlockPointerType())
        ImportedBlockDecls.insert(InnerBlockDeclRefs[i]->getDecl());
  }
  InnerDeclRefsCount.push_back(countOfInnerDecls);

  std::string FuncName;

  if (CurFunctionDef)
    FuncName = CurFunctionDef->getNameAsString();
  else if (GlobalVarDecl)
    FuncName = std::string(GlobalVarDecl->getNameAsString());

  std::string BlockNumber = utostr(Blocks.size()-1);

  std::string Tag = "__" + FuncName + "_block_impl_" + BlockNumber;
  std::string Func = "__" + FuncName + "_block_func_" + BlockNumber;

  // Get a pointer to the function type so we can cast appropriately.
  QualType BFT = convertFunctionTypeOfBlocks(Exp->getFunctionType());
  QualType FType = canonifyType(Context->getPointerType(BFT));

  FunctionDecl *FD;
  Expr *NewRep;

  // Simulate a contructor call...
  FD = SynthBlockInitFunctionDecl(Tag + "__INST");
  DeclRefExpr *DRE = new (Context)
      DeclRefExpr(*Context, FD, false, FType, VK_RValue, SourceLocation());

  SmallVector<Expr*, 4> InitExprs;

  // Initialize the block function.
  FD = SynthBlockInitFunctionDecl(Func);
  DeclRefExpr *Arg = new (Context)
      DeclRefExpr(*Context, FD, false, FD->getType(), VK_LValue,
                  SourceLocation());
  CastExpr *castExpr = NoTypeInfoCStyleCastExpr(Context, Context->VoidPtrTy,
                                                CK_BitCast, Arg);
  InitExprs.push_back(castExpr);

  // Initialize the block descriptor.
  std::string DescData = "__" + FuncName + "_block_desc_" + BlockNumber + "_DATA";

  VarDecl *NewVD = VarDecl::Create(*Context, TUDecl,
                                   SourceLocation(), SourceLocation(),
                                   &Context->Idents.get(DescData.c_str()),
                                   Context->VoidPtrTy, 0,
                                   SC_Static);

#if CLANG_VERSION_MAJOR >= 11
  UnaryOperator *DescRefExpr = UnaryOperator::Create(
      const_cast<ASTContext &>(*Context),
      new (Context) DeclRefExpr(*Context, NewVD, false, Context->VoidPtrTy,
                                VK_LValue, SourceLocation()),
      UO_AddrOf, Context->getPointerType(Context->VoidPtrTy), VK_RValue,
      OK_Ordinary, SourceLocation(), false, FPOptionsOverride());
#else
  UnaryOperator *DescRefExpr = new (Context) UnaryOperator(
        new (Context) DeclRefExpr(*Context, NewVD, false, Context->VoidPtrTy,
                                  VK_LValue, SourceLocation()),
        UO_AddrOf, Context->getPointerType(Context->VoidPtrTy), VK_RValue,
        OK_Ordinary, SourceLocation(), false);
#endif /* CLANG_VERSION_MAJOR >= 11 */

  InitExprs.push_back(DescRefExpr);

  // Add initializers for any closure decl refs.
  if (BlockDeclRefs.size()) {
    Expr *Exp;
    // Output all "by copy" declarations.
    for (SmallVector<ValueDecl*,8>::iterator I = BlockByCopyDecls.begin(),
         E = BlockByCopyDecls.end(); I != E; ++I) {
      if (isTopLevelBlockPointerType((*I)->getType())) {
        FD = SynthBlockInitFunctionDecl((*I)->getName());
        Arg = new (Context) DeclRefExpr(*Context, FD, false, FD->getType(),
                                        VK_LValue, SourceLocation());
        Exp = NoTypeInfoCStyleCastExpr(Context, Context->VoidPtrTy,
                                       CK_BitCast, Arg);
      } else {
        FD = SynthBlockInitFunctionDecl((*I)->getName());
        Exp = new (Context) DeclRefExpr(*Context, FD, false, FD->getType(),
                                        VK_LValue, SourceLocation());
        if (HasLocalVariableExternalStorage(*I)) {
          QualType QT = (*I)->getType();
          QT = Context->getPointerType(QT);
#if CLANG_VERSION_MAJOR >= 11
          Exp = UnaryOperator::Create(
              const_cast<ASTContext &>(*Context), Exp, UO_AddrOf, QT, VK_RValue,
              OK_Ordinary, SourceLocation(), false, FPOptionsOverride());
#else
          Exp = new (Context) UnaryOperator(Exp, UO_AddrOf, QT, VK_RValue,
                                            OK_Ordinary, SourceLocation(),
                                            false);
#endif /* CLANG_VERSION_MAJOR >= 11 */
        }

      }
      InitExprs.push_back(Exp);
    }
    // Output all "by ref" declarations.
    for (SmallVector<ValueDecl*,8>::iterator I = BlockByRefDecls.begin(),
         E = BlockByRefDecls.end(); I != E; ++I) {
      ValueDecl *ND = (*I);
      std::string Name(ND->getNameAsString());
      std::string RecName;
      RewriteByRefString(RecName, Name, ND, true);
      IdentifierInfo *II = &Context->Idents.get(RecName.c_str()
                                                + sizeof("struct"));
      RecordDecl *RD = RecordDecl::Create(*Context, TTK_Struct, TUDecl,
                                          SourceLocation(), SourceLocation(),
                                          II);
      assert(RD && "SynthBlockInitExpr(): Can't find RecordDecl");
      QualType castT = Context->getPointerType(Context->getTagDeclType(RD));

      FD = SynthBlockInitFunctionDecl((*I)->getName());
      Exp = new (Context) DeclRefExpr(*Context, FD, false, FD->getType(),
                                      VK_LValue, SourceLocation());
      bool isNestedCapturedVar = false;
      if (block)
        for (BlockDecl::capture_const_iterator ci = block->capture_begin(),
             ce = block->capture_end(); ci != ce; ++ci) {
          const VarDecl *variable = ci->getVariable();
          if (variable == ND && ci->isNested()) {
            assert (ci->isByRef() &&
                    "SynthBlockInitExpr - captured block variable is not byref");
            isNestedCapturedVar = true;
            break;
          }
        }
      // captured nested byref variable has its address passed. Do not take
      // its address again.
      if (!isNestedCapturedVar) {
#if CLANG_VERSION_MAJOR >= 11
          Exp = UnaryOperator::Create(
              const_cast<ASTContext &>(*Context), Exp, UO_AddrOf,
              Context->getPointerType(Exp->getType()), VK_RValue, OK_Ordinary,
              SourceLocation(), false, FPOptionsOverride());
#else
          Exp = new (Context) UnaryOperator(Exp, UO_AddrOf,
                                     Context->getPointerType(Exp->getType()),
                                     VK_RValue, OK_Ordinary, SourceLocation(),
                                     false);
#endif /* CLANG_VERSION_MAJOR >= 11 */
      }
      Exp = NoTypeInfoCStyleCastExpr(Context, castT, CK_BitCast, Exp);
      InitExprs.push_back(Exp);
    }
  }

  if (CS) {
    std::string Var     = Tag + "__VAR";
    std::string VarDecl = "struct " + Tag + " " + Var + ";";
    InsertText(CS->getBeginLoc().getLocWithOffset(1), VarDecl, false);
  }

  int flag = 0;
  if (ImportedBlockDecls.size()) {
    // generate BLOCK_HAS_COPY_DISPOSE(have helper funcs) | BLOCK_HAS_DESCRIPTOR
    flag = (BLOCK_HAS_COPY_DISPOSE | BLOCK_HAS_DESCRIPTOR);
  }
  unsigned IntSize =
    static_cast<unsigned>(Context->getTypeSize(Context->IntTy));
  Expr *FlagExp = IntegerLiteral::Create(*Context, llvm::APInt(IntSize, flag),
                                         Context->IntTy, SourceLocation());
  InitExprs.push_back(FlagExp);

#if CLANG_VERSION_MAJOR >= 12
  NewRep = CallExpr::Create(*Context, DRE, InitExprs, FType, VK_LValue,
                            SourceLocation(), FPOptionsOverride());
#else
  NewRep = CallExpr::Create(*Context, DRE, InitExprs, FType, VK_LValue,
                            SourceLocation());
#endif /* CLANG_VERSION_MAJOR >= 12 */

  NewRep = NoTypeInfoCStyleCastExpr(Context, Context->VoidPtrTy,
                                    CK_BitCast, NewRep);
  NewRep = NoTypeInfoCStyleCastExpr(Context, FType, CK_BitCast,
                                    NewRep);
  BlockDeclRefs.clear();
  BlockByRefDecls.clear();
  BlockByRefDeclsPtrSet.clear();
  BlockByCopyDecls.clear();
  BlockByCopyDeclsPtrSet.clear();
  ImportedBlockDecls.clear();
  return NewRep;
}

Stmt *RewriteBlocks::SynthesizeBlockCall(CallExpr *Exp, const Expr *BlockExp) {
  // Navigate to relevant type information.
  const BlockPointerType *CPT = 0;

  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(BlockExp)) {
    CPT = DRE->getType()->getAs<BlockPointerType>();
  } else if (const MemberExpr *MExpr = dyn_cast<MemberExpr>(BlockExp)) {
    CPT = MExpr->getType()->getAs<BlockPointerType>();
  }
  else if (const ParenExpr *PRE = dyn_cast<ParenExpr>(BlockExp)) {
    return SynthesizeBlockCall(Exp, PRE->getSubExpr());
  }
  else if (const ImplicitCastExpr *IEXPR = dyn_cast<ImplicitCastExpr>(BlockExp))
    CPT = IEXPR->getType()->getAs<BlockPointerType>();
  else if (const ConditionalOperator *CEXPR =
            dyn_cast<ConditionalOperator>(BlockExp)) {
    Expr *LHSExp = CEXPR->getLHS();
    Stmt *LHSStmt = SynthesizeBlockCall(Exp, LHSExp);
    Expr *RHSExp = CEXPR->getRHS();
    Stmt *RHSStmt = SynthesizeBlockCall(Exp, RHSExp);
    Expr *CONDExp = CEXPR->getCond();
    ConditionalOperator *CondExpr =
      new (Context) ConditionalOperator(CONDExp,
                                      SourceLocation(), cast<Expr>(LHSStmt),
                                      SourceLocation(), cast<Expr>(RHSStmt),
                                      Exp->getType(), VK_RValue, OK_Ordinary);
    return CondExpr;
  } else {
    assert(1 && "RewriteBlockClass: Bad type");
  }
  assert(CPT && "RewriteBlockClass: Bad type");
  const FunctionType *FT = CPT->getPointeeType()->getAs<FunctionType>();
  assert(FT && "RewriteBlockClass: Bad type");
  const FunctionProtoType *FTP = dyn_cast<FunctionProtoType>(FT);
  // FTP will be null for closures that don't take arguments.

  RecordDecl *RD = RecordDecl::Create(*Context, TTK_Struct, TUDecl,
                                      SourceLocation(), SourceLocation(),
                                      &Context->Idents.get("__block_impl"));
  QualType PtrBlock = Context->getPointerType(Context->getTagDeclType(RD));
  PtrBlock = canonifyType(PtrBlock);

  // Generate a funky cast.
  SmallVector<QualType, 8> ArgTypes;

  // Push the block argument type.
  ArgTypes.push_back(PtrBlock);
  if (FTP) {
    for (FunctionProtoType::param_type_iterator I = FTP->param_type_begin(),
         E = FTP->param_type_end(); I && (I != E); ++I) {
      QualType t = *I;
      ArgTypes.push_back(canonifyType(t));
    }
  }
  // Now do the pointer to function cast.
  QualType PtrToFuncCastType
    = getSimpleFunctionType(canonifyType(Exp->getType()), ArgTypes);

  PtrToFuncCastType = Context->getPointerType(PtrToFuncCastType);

  CastExpr *BlkCast = NoTypeInfoCStyleCastExpr(Context, PtrBlock,
                                               CK_BitCast,
                                               const_cast<Expr*>(BlockExp));
  // Don't forget the parens to enforce the proper binding.
  ParenExpr *PE = new (Context) ParenExpr(SourceLocation(), SourceLocation(),
                                          BlkCast);
  //PE->dump();

  FieldDecl *FD = FieldDecl::Create(*Context, 0, SourceLocation(),
                                    SourceLocation(),
                                    &Context->Idents.get("FuncPtr"),
                                    Context->VoidPtrTy, 0,
                                    /*BitWidth=*/0, /*Mutable=*/true,
                                    ICIS_NoInit);
  MemberExpr *ME = MemberExpr::CreateImplicit(
      *Context, PE, true, FD, FD->getType(), VK_LValue, OK_Ordinary);

  CastExpr *FunkCast = NoTypeInfoCStyleCastExpr(Context, PtrToFuncCastType,
                                                CK_BitCast, ME);
  PE = new (Context) ParenExpr(SourceLocation(), SourceLocation(), FunkCast);

  SmallVector<Expr*, 8> BlkExprs;
  // Add the implicit argument.
  BlkExprs.push_back(BlkCast);
  // Add the user arguments.
  for (CallExpr::arg_iterator I = Exp->arg_begin(),
       E = Exp->arg_end(); I != E; ++I) {
    BlkExprs.push_back(*I);
  }
#if CLANG_VERSION_MAJOR >= 12
  CallExpr *CE =
      CallExpr::Create(*Context, PE, BlkExprs, Exp->getType(), VK_RValue,
                       SourceLocation(), FPOptionsOverride());
#else
  CallExpr *CE = CallExpr::Create(*Context, PE, BlkExprs, Exp->getType(),
                                  VK_RValue, SourceLocation());
#endif /* CLANG_VERSION_MAJOR >= 12 */
  return CE;
}

void RewriteBlocks::InsertBlockLiteralsWithinFunction(FunctionDecl *FD)
{
  SourceLocation FunLocStart = FD->getTypeSpecStartLoc();
  StringRef FuncName = FD->getName();

  SynthesizeBlockLiterals(FunLocStart, FuncName);
}

void RewriteBlocks::HandleDeclInMainFile(Decl *D)
{
  switch (D->getKind()) {
    case Decl::CXXMethod:
    case Decl::Function: {
      FunctionDecl *FD = cast<FunctionDecl>(D);
      if (FD->isOverloadedOperator()) {
        return;
      }

      if (!FD->isThisDeclarationADefinition()) {
        break;
      }

      RewriteBlocksInFunctionProtoType(FD->getType(), FD);

      if (CompoundStmt *Body = dyn_cast_or_null<CompoundStmt>(FD->getBody())) {
        CurFunctionDef = FD;
        CurFunctionDeclToDeclareForBlock = FD;
        CurrentBody = Body;
        Body = cast<CompoundStmt>(RewriteStatement(Body, Body));
        FD->setBody(Body);
        CurrentBody = NULL;
        InsertBlockLiteralsWithinFunction(FD);
        CurFunctionDef = NULL;
        CurFunctionDeclToDeclareForBlock = NULL;
      }
      break;
    }
    case Decl::Var: {
      VarDecl *VD = cast<VarDecl>(D);
      if (isTopLevelBlockPointerType(VD->getType()))
        RewriteBlockPointerDecl(VD);
      else if (VD->getType()->isFunctionPointerType()) {
        CheckFunctionPointerDecl(VD->getType(), VD);
        if (VD->getInit()) {
          if (CStyleCastExpr *CE = dyn_cast<CStyleCastExpr>(VD->getInit())) {
            RewriteCastExpr(CE);
          }
        }
      } else if (VD->getType()->isRecordType()) {
        RecordDecl *RD = VD->getType()->getAs<RecordType>()->getDecl();
        if (RD->isCompleteDefinition())
          RewriteRecordBody(RD);
      }
      if (VD->getInit()) {
        GlobalVarDecl = VD;
        CurrentBody = VD->getInit();
        RewriteStatement(VD->getInit(), NULL);
        CurrentBody = 0;
        SynthesizeBlockLiterals(VD->getOuterLocStart(), VD->getName());
        GlobalVarDecl = 0;

        // This is needed for blocks.
        if (CStyleCastExpr *CE = dyn_cast<CStyleCastExpr>(VD->getInit())) {
            RewriteCastExpr(CE);
        }
      }
      break;
    }
    case Decl::TypeAlias:
    case Decl::Typedef: {
      if (TypedefNameDecl *TD = dyn_cast<TypedefNameDecl>(D)) {
        if (isTopLevelBlockPointerType(TD->getUnderlyingType()))
          RewriteBlockPointerDecl(TD);
        else if (TD->getUnderlyingType()->isFunctionPointerType())
          CheckFunctionPointerDecl(TD->getUnderlyingType(), TD);
      }
      break;
    }
    case Decl::CXXRecord:
    case Decl::Record: {
      RecordDecl *RD = cast<RecordDecl>(D);
      if (RD->isCompleteDefinition())
        RewriteRecordBody(RD);
      break;
    }
    case Decl::Namespace: {
      NamespaceDecl *NSD = dyn_cast<NamespaceDecl>(D);
      // Recurse into linkage specifications
      for (DeclContext::decl_iterator DI = NSD->decls_begin(),
                                   DIEnd = NSD->decls_end();
           DI != DIEnd; ++DI)
        HandleDeclInMainFile(*DI);
      break;
    }
    default:
      break;
  }
}

Stmt *RewriteBlocks::RewriteStatement(Stmt *S, CompoundStmt *CS)
{
  //SourceRange OrigStmtRange = S->getSourceRange();

  // Perform a bottom up rewrite of all children.
  for (Stmt *&SubStmt : S->children())
    if (SubStmt) {
      Stmt *childStmt = (SubStmt);
      CompoundStmt *cp = dyn_cast<CompoundStmt>(childStmt);
      Stmt *newStmt = RewriteStatement(childStmt, cp ? cp : CS);
      if (newStmt) {
        SubStmt = newStmt;
      }
    }

  if (BlockExpr *BE = dyn_cast<BlockExpr>(S)) {
    SmallVector<DeclRefExpr *, 8> InnerBlockDeclRefs;
    llvm::SmallPtrSet<const DeclContext *, 8> InnerContexts;
    InnerContexts.insert(BE->getBlockDecl());
    ImportedLocalExternalDecls.clear();
    GetInnerBlockDeclRefExprs(BE->getBody(),
                              InnerBlockDeclRefs, InnerContexts);
    // Rewrite the block body in place.
    Stmt *SaveCurrentBody = CurrentBody;
    CurrentBody = BE->getBody();
    // block literal on rhs of a property-dot-sytax assignment
    // must be replaced by its synthesize ast so getRewrittenText
    // works as expected. In this case, what actually ends up on RHS
    // is the blockTranscribed which is the helper function for the
    // block literal; as in: self.c = ^() {[ace ARR];};
    bool saveDisableReplaceStmt = DisableReplaceStmt;
    DisableReplaceStmt = false;
    CompoundStmt *cp = cast_or_null<CompoundStmt>(BE->getBody());
    RewriteStatement(cp, cp);
    DisableReplaceStmt = saveDisableReplaceStmt;
    CurrentBody = SaveCurrentBody;
    ImportedLocalExternalDecls.clear();
    // Now we snarf the rewritten text and stash it away for later use.
    std::string Str = Rewrite.getRewrittenText(BE->getSourceRange());
    const char *Paren = ::strchr(Str.c_str(), '{');

    if (Str.size() == 0 || !Paren) {
      Diags.Report(Context->getFullLoc(BE->getBeginLoc()), RewriteFailedDiag)
                   << BE->getSourceRange();
      return S;
    }

    llvm::StringRef Str2(Paren + 1);

    RewrittenBlockExprs[BE] = MakeSharpLine(BE->getBody()->getBeginLoc())
      + Str2.str();

    Stmt *blockTranscribed = SynthBlockInitExpr(BE, InnerBlockDeclRefs, CS);

    //blockTranscribed->dump();
    ReplaceStmt(S, blockTranscribed, true);
    return blockTranscribed;
  }

  // Need to check for protocol refs (id <P>, Foo <P> *) in variable decls
  // and cast exprs.
  if (DeclStmt *DS = dyn_cast<DeclStmt>(S)) {
    // Blocks rewrite rules.
    for (DeclStmt::decl_iterator DI = DS->decl_begin(), DE = DS->decl_end();
         DI != DE; ++DI) {
      Decl *SD = *DI;
      if (ValueDecl *ND = dyn_cast<ValueDecl>(SD)) {
        if (isTopLevelBlockPointerType(ND->getType()))
          RewriteBlockPointerDecl(ND);
        else if (ND->getType()->isFunctionPointerType())
          CheckFunctionPointerDecl(ND->getType(), ND);
        if (VarDecl *VD = dyn_cast<VarDecl>(SD)) {
          if (VD->hasAttr<BlocksAttr>()) {
            static unsigned uniqueByrefDeclCount = 0;
            assert(!BlockByRefDeclNo.count(ND) &&
              "RewriteFunctionBodyOrGlobalInitializer: Duplicate byref decl");
            BlockByRefDeclNo[ND] = uniqueByrefDeclCount++;
            RewriteByRefVar(VD);
          }
        }
      }
      if (TypedefNameDecl *TD = dyn_cast<TypedefNameDecl>(SD)) {
        if (isTopLevelBlockPointerType(TD->getUnderlyingType()))
          RewriteBlockPointerDecl(TD);
        else if (TD->getUnderlyingType()->isFunctionPointerType())
          CheckFunctionPointerDecl(TD->getUnderlyingType(), TD);
      }
    }
  }

  // Handle blocks rewriting.
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(S)) {
    ValueDecl *VD = DRE->getDecl();
    if (VD->hasAttr<BlocksAttr>())
      return RewriteBlockDeclRefExpr(DRE);
    if (HasLocalVariableExternalStorage(VD))
      return RewriteLocalVariableExternalStorage(DRE);
  }

  if (CallExpr *CE = dyn_cast<CallExpr>(S)) {
    if (CE->getCallee()->getType()->isBlockPointerType()) {
      Stmt *BlockCall = SynthesizeBlockCall(CE, CE->getCallee());
      ReplaceStmt(S, BlockCall, true);
      return BlockCall;
    }
  }
  if (CStyleCastExpr *CE = dyn_cast<CStyleCastExpr>(S)) {
    RewriteCastExpr(CE);
  }
  // Return this stmt unmodified.
  return S;
}

bool RewriteBlocks::HandleTopLevelSingleDecl(Decl *D)
{
  if (Diags.hasErrorOccurred())
    return false;

  // Two cases: either the decl could be in the main file, or it could be in a
  // #included file.  If the former, rewrite it now.  If the later, check to see
  // if we rewrote the #include/#import.
  SourceLocation Loc = D->getLocation();
  Loc = SM->getExpansionLoc(Loc);

  // If this is for a builtin, ignore it.
  if (Loc.isInvalid()) return true;

  if (LinkageSpecDecl *LSD = dyn_cast<LinkageSpecDecl>(D)) {
    // Recurse into linkage specifications
    for (DeclContext::decl_iterator DI = LSD->decls_begin(),
                                 DIEnd = LSD->decls_end();
         DI != DIEnd; ) {
      if (!HandleTopLevelSingleDecl(*DI)) {
        return false;
      }
      ++DI;
    }
  }
  // If we have a decl in the main file, see if we should rewrite it.
  if (SM->isWrittenInMainFile(Loc))
    HandleDeclInMainFile(D);
  return !Diags.hasErrorOccurred();
}

bool RewriteBlocks::HandleTopLevelDecl(DeclGroupRef D)
{
  if (Diags.hasErrorOccurred())
    return false;

  for (DeclGroupRef::iterator I = D.begin(), E = D.end(); I != E; ++I) {
    if (!HandleTopLevelSingleDecl(*I)) {
      return false;
    }
  }
  return !Diags.hasErrorOccurred();
}


void RewriteBlocks::HandleTranslationUnit(ASTContext &C)
{
  if (Diags.hasErrorOccurred())
    return;

  InsertText(SM->getLocForStartOfFile(MainFileID), Preamble, false);

  // Get the buffer corresponding to MainFileID.  If we haven't changed it, then
  // we are done.
  if (const RewriteBuffer *RewriteBuf =
      Rewrite.getRewriteBufferFor(MainFileID)) {
    //printf("Changed:\n");
    *OutFile << std::string(RewriteBuf->begin(), RewriteBuf->end());
  } else {
    llvm::errs() << "No changes\n";
  }

  OutFile->flush();
}

// vim:set ts=2 sts=2:
