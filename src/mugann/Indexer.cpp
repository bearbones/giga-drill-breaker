#include "giga_drill/mugann/Indexer.h"
#include "giga_drill/compat/ClangVersion.h"

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

void IndexerVisitor::setASTContext(clang::ASTContext *ctx) {
  astContext_ = ctx;
}

bool IndexerVisitor::VisitCXXMethodDecl(clang::CXXMethodDecl *decl) {
  if (decl->isImplicit())
    return true;

  if (!decl->isThisDeclarationADefinition())
    return true;

  if (decl->getPreviousDecl())
    return true;

  auto *parent = llvm::dyn_cast<clang::CXXRecordDecl>(decl->getParent());
  if (!parent)
    return true;

  CoveragePropertyEntry entry;
  entry.qualifiedName = decl->getQualifiedNameAsString();
  entry.headerPath = getFilePath(decl->getLocation());
  entry.sourceLine = sm_.getSpellingLineNumber(decl->getLocation());
  entry.enclosingClass = parent->getQualifiedNameAsString();

  if (astContext_)
    entry.gvaLinkage =
        static_cast<int>(astContext_->GetGVALinkageForFunction(decl));

  entry.isInlined = decl->isInlined();
  entry.isConstexpr = decl->isConstexpr();
  entry.isDefaulted = decl->isDefaulted();
  entry.isTrivial = decl->isTrivial();
  entry.isVirtual = decl->isVirtual();
  entry.isStaticMethod = decl->isStatic();
  entry.isImplicitlyInstantiable = decl->isImplicitlyInstantiable();
  entry.templatedKind = static_cast<int>(decl->getTemplatedKind());
  entry.storageClass = static_cast<int>(decl->getStorageClass());
  entry.formalLinkage =
      static_cast<int>(decl->getLinkageAndVisibility().getLinkage());

  if (auto *body = decl->getBody())
    entry.bodyStmtCount = countStmts(body);

  // Build human-readable signature.
  entry.signature = decl->getReturnType().getAsString() + " " +
                    decl->getQualifiedNameAsString() + "(";
  for (unsigned i = 0; i < decl->getNumParams(); ++i) {
    if (i > 0)
      entry.signature += ", ";
    entry.signature += decl->getParamDecl(i)->getType().getAsString();
  }
  entry.signature += ")";
  if (decl->isConst())
    entry.signature += " const";

  index_.addCoverageProperty(std::move(entry));
  return true;
}

unsigned IndexerVisitor::countStmts(const clang::Stmt *s,
                                    unsigned limit) const {
  if (!s || limit == 0)
    return 0;
  unsigned count = 1;
  for (auto *child : s->children()) {
    if (child) {
      count += countStmts(child, limit - count);
      if (count >= limit)
        return limit;
    }
  }
  return count;
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
  visitor_.setASTContext(&context);
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
