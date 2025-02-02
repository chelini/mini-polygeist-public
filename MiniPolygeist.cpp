#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Index/IR/IndexDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"

#include "llvm/Support/CommandLine.h"

using namespace clang;
using namespace clang::tooling;
using namespace mlir;
using namespace llvm;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory MiniPolygeistCategory("mini-polygeist options");

static cl::opt<bool>
    DumpAST("dump-ast",
            cl::desc("Dump the AST for the current translation unit"),
            cl::cat(MiniPolygeistCategory));

static cl::opt<bool>
    DumpFuncDecl("dump-func-decl",
                 cl::desc("Dump the AST for all function declarations"),
                 cl::cat(MiniPolygeistCategory));

struct MiniPolygeistASTConsumer : public ASTConsumer {
  explicit MiniPolygeistASTConsumer(MLIRContext &ctx)
      : builder(&ctx), module(ModuleOp::create(UnknownLoc::get(&ctx))) {}
  void HandleTranslationUnit(ASTContext &ctx) override {

    // This is our entry point into the AST for the current translation unit.
    TranslationUnitDecl *tuDecl = ctx.getTranslationUnitDecl();
    if (DumpAST) {
      tuDecl->dump();
      return;
    }
    for (auto decl : tuDecl->decls()) {
      if (auto funcDecl = dyn_cast<FunctionDecl>(decl)) {
        translateToMLIRFunction(funcDecl);
      }
    }
  }

  mlir::Location getMLIRLocation(clang::SourceLocation loc) {
    auto ctx = module->getContext();
    return UnknownLoc::get(ctx);
  }

  mlir::Type getMLIRTypeFromQualType(QualType ty) {
    const BuiltinType *bTy = ty->getAs<BuiltinType>();
    if (bTy && bTy->getKind() == BuiltinType::Int)
      return builder.getI32Type();
    return mlir::Type();
  }

  mlir::Value visitExpr(const Expr *expr) {
    if (const auto *binaryOperator = dyn_cast<BinaryOperator>(expr)) {
      mlir::Value lhs = visitExpr(binaryOperator->getLHS());
      mlir::Value rhs = visitExpr(binaryOperator->getRHS());
      if (binaryOperator->getOpcode() == BinaryOperator::Opcode::BO_Add) {
        auto loc = getMLIRLocation(binaryOperator->getOperatorLoc());
        return builder.create<arith::AddIOp>(loc, lhs, rhs);
      }
      assert(0 && "only BO_Add is supported at the moment.");
      return mlir::Value();
    }

    // TODO: Not a good way to handle ImplicitCastExpr.
    if (const auto *implicitCast = dyn_cast<ImplicitCastExpr>(expr)) {
      return visitExpr(implicitCast->getSubExpr());
    }

    if (const auto *declRef = dyn_cast<DeclRefExpr>(expr)) {
      const ValueDecl *decl = declRef->getDecl();
      auto it = valueMap.find(decl);
      if (it != valueMap.end())
        return it->second;
      assert(0 &&
             "Something is wrong. I cannot find the reference in the map.");
    }

    assert(0 && "expression not handled yet.");
    return mlir::Value();
  }

  void visitReturnStmt(const ReturnStmt *stmt) {
    mlir::Location loc = getMLIRLocation(stmt->getBeginLoc());
    const Expr *retExpr = stmt->getRetValue();
    if (retExpr) {
      auto retVal = visitExpr(retExpr);
      builder.create<func::ReturnOp>(loc, retVal);
      return;
    }
    builder.create<func::ReturnOp>(loc);
  }

  void visitStmt(const Stmt *stmt) {
    if (const auto *returnStmt = dyn_cast<ReturnStmt>(stmt)) {
      visitReturnStmt(returnStmt);
    }
  }

  void translateToMLIRFunction(const FunctionDecl *funcDecl) {
    if (DumpFuncDecl) {
      funcDecl->dump();
      return;
    }

    // Set the insertion point of our builder at the end of the module body.
    builder.setInsertionPointToEnd(module->getBody());

    // Build our function by:
    // 1) translating a clang SourceLocation to an MLIR location.
    // 2) extracting the function name from the FunctionDecl.
    // 3) Converting each parameter type (QualType) to an MLIR type.
    mlir::Location loc = getMLIRLocation(funcDecl->getLocation());
    auto funcName = funcDecl->getNameAsString();
    SmallVector<mlir::Type> inputTys;
    for (const ParmVarDecl *param : funcDecl->parameters()) {
      mlir::Type ty = getMLIRTypeFromQualType(param->getOriginalType());
      inputTys.push_back(ty);
    }
    mlir::Type outputTy = getMLIRTypeFromQualType(funcDecl->getReturnType());

    auto funcType = builder.getFunctionType(inputTys, outputTy);
    auto funcOp = builder.create<func::FuncOp>(loc, funcName, funcType);

    // Add an entry block to our function.
    auto &entryBlock = *funcOp.addEntryBlock();
    // ... and set the insertion point into this block.
    builder.setInsertionPointToStart(&entryBlock);

    // We need to keep a map that connect ValDecl to MLIR Values.
    for (auto [index, param] : llvm::enumerate(funcDecl->parameters()))
      valueMap[param] = funcOp.getArgument(index);

    if (auto compoundStmt = dyn_cast<CompoundStmt>(funcDecl->getBody())) {
      for (auto stmt : compoundStmt->body()) {
        visitStmt(stmt);
      }
    }

    if (failed(mlir::verify(module.get()))) {
      llvm::errs() << "FAILED to verify module\n";
      module->dump();
      return;
    }
    llvm::outs() << "Printing the module...\n";
    module->print(llvm::outs());
  }

private:
  OpBuilder builder;
  // The MLIR module is the top-level (or container) IR where all the rest of
  // the IR will live.
  OwningOpRef<ModuleOp> module;
  std::unordered_map<const ValueDecl *, mlir::Value> valueMap;
};

struct MiniPolygeistFrontendAction : public ASTFrontendAction {
  MiniPolygeistFrontendAction() {
    // Load MLIR dialects we need.
    context.getOrLoadDialect<func::FuncDialect>();
    context.getOrLoadDialect<arith::ArithDialect>();
  }
  // Overridden method from ASTFrontendAction. The compilerInstance provides
  // access to various compilation resources, while file is the name of the
  // source file being processed.
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef file) override {
    return std::make_unique<MiniPolygeistASTConsumer>(context);
  }

private:
  MLIRContext context;
};

int main(int argc, const char **argv) {

  auto ExpectedParser =
      CommonOptionsParser::create(argc, argv, MiniPolygeistCategory);
  if (!ExpectedParser) {
    // Fail gracefully for unsupported options.
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser &OptionsParser = ExpectedParser.get();

  // This line creates a ClangTool object, which is the main interface for
  // running Clang-based tools. It takes two arguments:
  // OptionsParser.getCompilations(): This retrieves the compilation database,
  // which contains information about how each source file should be compiled,
  // including compiler flags and include paths
  // OptionsParser.getSourcePathList(): This provides a list of source file
  // paths that the tool will process.
  ClangTool tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  // This line runs the Clang tool and returns its result. Let's break it down
  // further:
  // - newFrontendActionFactory<MiniPolygeistFrontendAction>(): This creates a
  // new FrontendActionFactory for the custom MiniPolygeistFrontendAction. A
  // FrontendActionFactory is responsible for creating FrontendAction objects,
  // which define the actions to be performed on each translation unit.
  // - .get(): This retrieves the raw pointer from the unique_ptr returned by
  // newFrontendActionFactory.
  // - Tool.run(...): This executes the Clang tool,
  // applying the specified FrontendAction to each source file in the
  // compilation database. The run method will create a new FrontendAction for
  // each translation unit (source file) it processes. This allows the tool to
  // perform custom operations on the AST or other aspects of the code for each
  // file
  return tool.run(
      newFrontendActionFactory<MiniPolygeistFrontendAction>().get());
}