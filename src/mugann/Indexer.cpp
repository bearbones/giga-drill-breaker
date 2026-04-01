#include "giga_drill/mugann/Indexer.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Frontend/CompilerInstance.h"

namespace giga_drill {

// --- IndexerVisitor ---

IndexerVisitor::IndexerVisitor(GlobalIndex &index, clang::SourceManager &sm)
    : index_(index), sm_(sm) {}

bool IndexerVisitor::VisitFunctionDecl(clang::FunctionDecl *decl) {
  // Skip implicit declarations (compiler-generated).
  if (decl->isImplicit())
    return true;

  // Skip deduction guides — they're handled separately.
  if (llvm::isa<clang::CXXDeductionGuideDecl>(decl))
    return true;

  // Skip function templates (we want concrete overloads).
  // Template specializations are fine.
  if (decl->getDescribedFunctionTemplate())
    return true;

  // Only index definitions or the first declaration to avoid duplicates
  // from multiple includes. We prefer to index every unique declaration
  // since different headers may declare different overloads.
  if (decl->getPreviousDecl())
    return true;

  FunctionOverloadEntry entry;
  entry.qualifiedName = decl->getQualifiedNameAsString();
  entry.headerPath = getFilePath(decl->getLocation());
  entry.returnType = decl->getReturnType().getAsString();
  entry.sourceLine = sm_.getSpellingLineNumber(decl->getLocation());

  for (const auto *param : decl->parameters()) {
    entry.paramTypes.push_back(param->getType().getAsString());
  }

  index_.addFunctionOverload(std::move(entry));
  return true;
}

bool IndexerVisitor::VisitCXXDeductionGuideDecl(
    clang::CXXDeductionGuideDecl *decl) {
  if (decl->isImplicit())
    return true;

  // Skip redeclarations.
  if (decl->getPreviousDecl())
    return true;

  DeductionGuideEntry entry;
  entry.templateName = decl->getDeducedTemplate()->getQualifiedNameAsString();
  entry.headerPath = getFilePath(decl->getLocation());
  entry.deducedType = decl->getReturnType().getAsString();
  entry.sourceLine = sm_.getSpellingLineNumber(decl->getLocation());

  for (const auto *param : decl->parameters()) {
    entry.paramTypes.push_back(param->getType().getAsString());
  }

  index_.addDeductionGuide(std::move(entry));
  return true;
}

std::string IndexerVisitor::getFilePath(clang::SourceLocation loc) const {
  auto fileEntry = sm_.getFileEntryRefForID(sm_.getFileID(
      sm_.getSpellingLoc(loc)));
  if (fileEntry)
    return std::string(fileEntry->getName());
  return "<unknown>";
}

// --- IndexerConsumer ---

IndexerConsumer::IndexerConsumer(GlobalIndex &index, clang::SourceManager &sm)
    : visitor_(index, sm) {}

void IndexerConsumer::HandleTranslationUnit(clang::ASTContext &context) {
  visitor_.TraverseDecl(context.getTranslationUnitDecl());
}

// --- IndexerAction ---

IndexerAction::IndexerAction(GlobalIndex &index) : index_(index) {}

std::unique_ptr<clang::ASTConsumer>
IndexerAction::CreateASTConsumer(clang::CompilerInstance &ci,
                                 llvm::StringRef /*file*/) {
  return std::make_unique<IndexerConsumer>(index_, ci.getSourceManager());
}

// --- IndexerActionFactory ---

IndexerActionFactory::IndexerActionFactory(GlobalIndex &index)
    : index_(index) {}

std::unique_ptr<clang::FrontendAction> IndexerActionFactory::create() {
  return std::make_unique<IndexerAction>(index_);
}

} // namespace giga_drill
