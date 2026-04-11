#include "giga_drill/mugann/Analyzer.h"
#include "giga_drill/mugann/Indexer.h"
#include "giga_drill/compat/ClangVersion.h"
#include "giga_drill/compat/ToolAdjusters.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CompilationDatabase.h"

namespace giga_drill {

// --- AnalyzerVisitor ---

AnalyzerVisitor::AnalyzerVisitor(const GlobalIndex &index,
                                 clang::SourceManager &sm,
                                 std::vector<Diagnostic> &diagnostics)
    : index_(index), sm_(sm), diagnostics_(diagnostics) {}

bool AnalyzerVisitor::VisitCallExpr(clang::CallExpr *expr) {
  // Only analyze calls in the main file (not in included headers).
  if (!sm_.isInMainFile(expr->getBeginLoc()))
    return true;

  auto *callee = expr->getDirectCallee();
  if (!callee)
    return true;

  // We're interested in calls resolved via ADL — unqualified calls where
  // namespace lookup was triggered by argument types.
  // A simple heuristic: the call uses an unqualified name (no DeclRefExpr
  // with a nested-name-specifier) and the callee is in a namespace
  // associated with one of its argument types.

  // Check if the callee is a namespace-scope function (ADL candidate).
  if (!callee->getDeclContext()->isNamespace())
    return true;

  // Skip if the callee was explicitly qualified (e.g., MathLib::scale).
  if (auto *dre = llvm::dyn_cast_or_null<clang::DeclRefExpr>(
          expr->getCallee()->IgnoreParenImpCasts())) {
    if (dre->hasQualifier())
      return true;
  }

  std::string qualifiedName = callee->getQualifiedNameAsString();

  // Query the global index for all known overloads.
  auto overloads = index_.findOverloads(qualifiedName);
  if (overloads.size() <= 1)
    return true; // No alternative overloads exist globally.

  populateIncludedFiles();

  // Collect overloads that exist in the global index but are NOT visible
  // in this translation unit.
  std::string calleePath = getFilePath(callee->getLocation());

  // Build the resolved callee's parameter type signature for comparison.
  std::vector<std::string> resolvedParamTypes;
  for (unsigned i = 0; i < callee->getNumParams(); ++i)
    resolvedParamTypes.push_back(
        callee->getParamDecl(i)->getType().getAsString());

  for (const auto *entry : overloads) {
    // Skip overloads that match the resolved callee's signature
    // (same parameter types). These are the "same" overload even if
    // declared in a different file.
    if (entry->paramTypes == resolvedParamTypes)
      continue;

    // Check if this overload's header is included in the current TU.
    if (isFileIncluded(entry->headerPath))
      continue;

    // This overload exists but is invisible. Check if it could be a
    // better match by looking at argument types.
    // For now, flag any invisible overload with the same arity as
    // suspicious — a more sophisticated analysis would do overload
    // resolution ranking.
    if (entry->paramTypes.size() !=
        static_cast<size_t>(expr->getNumArgs()))
      continue;

    // Build a human-readable signature for the invisible overload.
    std::string betterSig = entry->qualifiedName + "(";
    for (size_t i = 0; i < entry->paramTypes.size(); ++i) {
      if (i > 0)
        betterSig += ", ";
      betterSig += entry->paramTypes[i];
    }
    betterSig += ")";

    // Build a signature for the resolved overload.
    std::string resolvedSig = qualifiedName + "(";
    for (unsigned i = 0; i < callee->getNumParams(); ++i) {
      if (i > 0)
        resolvedSig += ", ";
      resolvedSig += callee->getParamDecl(i)->getType().getAsString();
    }
    resolvedSig += ")";

    Diagnostic diag;
    diag.kind = Diagnostic::ADL_Fallback;
    diag.callLocation = formatLocation(expr->getBeginLoc());
    diag.resolvedDecl = resolvedSig;
    diag.betterDecl = betterSig;
    diag.missingHeader = entry->headerPath;
    diag.message = "Fragile ADL resolution: " + betterSig + " exists in " +
                   entry->headerPath +
                   " but is not visible here. The current call resolves to " +
                   resolvedSig + ". Include " + entry->headerPath +
                   " or explicitly qualify the call.";

    diagnostics_.push_back(std::move(diag));
  }

  return true;
}

bool AnalyzerVisitor::VisitVarDecl(clang::VarDecl *decl) {
  // Only analyze declarations in the main file.
  if (!sm_.isInMainFile(decl->getBeginLoc()))
    return true;

  // Check if this variable was declared with CTAD.
  auto *tsl = decl->getTypeSourceInfo();
  if (!tsl)
    return true;

  auto type = decl->getType();

  // Look for template specialization types that resulted from CTAD.
  const clang::TemplateSpecializationType *tst = nullptr;

  // Check the desugared type for a template specialization.
  if (auto *recordType = type->getAs<clang::RecordType>()) {
    if (auto *ctsd = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(
            recordType->getDecl())) {
      // Check if this specialization was via deduction (CTAD) rather than
      // explicit template arguments.
      // A heuristic: if the VarDecl has an initializer with a
      // CXXConstructExpr and the declared type uses auto or a deduced
      // template specialization, this is likely CTAD.
      if (!decl->hasInit())
        return true;

      auto *templateDecl = ctsd->getSpecializedTemplate();
      if (!templateDecl)
        return true;

      std::string templateName = templateDecl->getQualifiedNameAsString();

      // Query global index for deduction guides.
      auto guides = index_.findDeductionGuides(templateName);
      if (guides.empty())
        return true;

      populateIncludedFiles();

      std::string deducedType = type.getAsString();

      for (const auto *entry : guides) {
        // Skip guides that are visible in this TU.
        if (isFileIncluded(entry->headerPath))
          continue;

        // An invisible deduction guide exists. If it would produce
        // a different type, flag it.
        if (entry->deducedType == deducedType)
          continue;

        Diagnostic diag;
        diag.kind = Diagnostic::CTAD_Fallback;
        diag.callLocation = formatLocation(decl->getBeginLoc());
        diag.resolvedDecl = deducedType;
        diag.betterDecl = entry->deducedType;
        diag.missingHeader = entry->headerPath;
        diag.message =
            "Fragile CTAD resolution: deduction guide in " +
            entry->headerPath + " would deduce " + entry->deducedType +
            " but is not visible here. The current deduction produces " +
            deducedType + ". Include " + entry->headerPath +
            " to use the explicit guide.";

        diagnostics_.push_back(std::move(diag));
      }
    }
  }

  return true;
}

void AnalyzerVisitor::populateIncludedFiles() const {
  if (includedFilesPopulated_)
    return;
  includedFilesPopulated_ = true;

  // Collect all files that are part of this translation unit.
  for (auto it = sm_.fileinfo_begin(); it != sm_.fileinfo_end(); ++it) {
    includedFiles_.insert(std::string(it->first.getName()));
  }
}

bool AnalyzerVisitor::isFileIncluded(const std::string &path) const {
  return includedFiles_.count(path) > 0;
}

std::string
AnalyzerVisitor::formatLocation(clang::SourceLocation loc) const {
  auto spellingLoc = sm_.getSpellingLoc(loc);
  auto file = sm_.getFilename(spellingLoc);
  unsigned line = sm_.getSpellingLineNumber(spellingLoc);
  unsigned col = sm_.getSpellingColumnNumber(spellingLoc);
  return std::string(file) + ":" + std::to_string(line) + ":" +
         std::to_string(col);
}

std::string
AnalyzerVisitor::getFilePath(clang::SourceLocation loc) const {
  auto fileEntry = sm_.getFileEntryRefForID(
      sm_.getFileID(sm_.getSpellingLoc(loc)));
  if (fileEntry)
    return std::string(fileEntry->getName());
  return "<unknown>";
}

// --- AnalyzerConsumer ---

AnalyzerConsumer::AnalyzerConsumer(const GlobalIndex &index,
                                   clang::SourceManager &sm,
                                   std::vector<Diagnostic> &diagnostics)
    : visitor_(index, sm, diagnostics) {}

void AnalyzerConsumer::HandleTranslationUnit(clang::ASTContext &context) {
  visitor_.TraverseDecl(context.getTranslationUnitDecl());
}

// --- AnalyzerAction ---

AnalyzerAction::AnalyzerAction(const GlobalIndex &index,
                               std::vector<Diagnostic> &diags)
    : index_(index), diagnostics_(diags) {}

std::unique_ptr<clang::ASTConsumer>
AnalyzerAction::CreateASTConsumer(clang::CompilerInstance &ci,
                                  llvm::StringRef /*file*/) {
  return std::make_unique<AnalyzerConsumer>(index_, ci.getSourceManager(),
                                           diagnostics_);
}

// --- AnalyzerActionFactory ---

AnalyzerActionFactory::AnalyzerActionFactory(const GlobalIndex &index,
                                             std::vector<Diagnostic> &diags)
    : index_(index), diagnostics_(diags) {}

std::unique_ptr<clang::FrontendAction> AnalyzerActionFactory::create() {
  return std::make_unique<AnalyzerAction>(index_, diagnostics_);
}

// --- Coverage Analysis ---

static std::string gvaLinkageName(int gva) {
  switch (gva) {
  case 0:
    return "GVA_Internal";
  case 1:
    return "GVA_AvailableExternally";
  case 2:
    return "GVA_DiscardableODR";
  case 3:
    return "GVA_StrongODR";
  case 4:
    return "GVA_StrongExternal";
  default:
    return "Unknown(" + std::to_string(gva) + ")";
  }
}

void analyzeCoverageProperties(const GlobalIndex &index,
                               std::vector<Diagnostic> &diagnostics) {
  for (const auto &className : index.allIndexedClasses()) {
    auto methods = index.findClassMethods(className);
    if (methods.empty())
      continue;

    // Phase A: Flag individual methods with risky GVA linkage.
    for (const auto *m : methods) {
      if (m->gvaLinkage == 2 /* GVA_DiscardableODR */) {
        Diagnostic diag;
        diag.kind = Diagnostic::Coverage_DiscardableODR;
        diag.callLocation =
            m->headerPath + ":" + std::to_string(m->sourceLine);
        diag.resolvedDecl = m->signature;
        diag.message =
            "Coverage risk: " + m->signature +
            " has GVA_DiscardableODR linkage. COMDAT deduplication at link "
            "time may replace the instrumented definition with an "
            "uninstrumented one from another TU, producing hash 0x0 in "
            "coverage data.";
        diagnostics.push_back(std::move(diag));
      }
      if (m->gvaLinkage == 1 /* GVA_AvailableExternally */) {
        Diagnostic diag;
        diag.kind = Diagnostic::Coverage_AvailableExternally;
        diag.callLocation =
            m->headerPath + ":" + std::to_string(m->sourceLine);
        diag.resolvedDecl = m->signature;
        diag.message =
            "Coverage risk: " + m->signature +
            " has GVA_AvailableExternally linkage. The optimizer may "
            "inline the body and then discard the standalone definition, "
            "eliminating coverage instrumentation.";
        diagnostics.push_back(std::move(diag));
      }
    }

    if (methods.size() < 2)
      continue;

    // Phase B: Compare sibling methods for GVA linkage mismatch.
    std::unordered_map<int, std::vector<const CoveragePropertyEntry *>> byGVA;
    for (const auto *m : methods)
      byGVA[m->gvaLinkage].push_back(m);

    if (byGVA.size() > 1) {
      size_t maxGroupSize = 0;
      int majorityGVA = -1;
      for (const auto &kv : byGVA) {
        if (kv.second.size() > maxGroupSize) {
          maxGroupSize = kv.second.size();
          majorityGVA = kv.first;
        }
      }

      for (const auto &kv : byGVA) {
        if (kv.first == majorityGVA)
          continue;
        for (const auto *m : kv.second) {
          Diagnostic diag;
          diag.kind = Diagnostic::Coverage_GVAMismatch;
          diag.callLocation =
              m->headerPath + ":" + std::to_string(m->sourceLine);
          diag.resolvedDecl = m->signature;
          diag.betterDecl =
              "majority linkage: " + gvaLinkageName(majorityGVA);
          diag.message =
              "Coverage divergence in " + className + ": " + m->signature +
              " has " + gvaLinkageName(kv.first) + " linkage, but " +
              std::to_string(maxGroupSize) +
              " other method(s) in the same class have " +
              gvaLinkageName(majorityGVA) +
              " linkage. This difference may cause divergent coverage "
              "instrumentation behavior.";
          diagnostics.push_back(std::move(diag));
        }
      }
    }

    // Phase C: Property divergence within same-GVA groups.
    for (const auto &kv : byGVA) {
      if (kv.second.size() < 2)
        continue;
      for (size_t i = 0; i < kv.second.size(); ++i) {
        for (size_t j = i + 1; j < kv.second.size(); ++j) {
          const auto *a = kv.second[i];
          const auto *b = kv.second[j];
          // AST node counting: a simple `return x_` getter has ~5 nodes
          // (CompoundStmt, ReturnStmt, ImplicitCastExpr, MemberExpr,
          // CXXThisExpr). Threshold of 8 captures one-line getters/setters.
          bool aTrivial = (a->bodyStmtCount <= 8 || a->isTrivial);
          bool bTrivial = (b->bodyStmtCount <= 8 || b->isTrivial);
          if (aTrivial != bTrivial) {
            const auto *complex = aTrivial ? b : a;
            const auto *trivial = aTrivial ? a : b;
            Diagnostic diag;
            diag.kind = Diagnostic::Coverage_PropertyDivergence;
            diag.callLocation =
                complex->headerPath + ":" +
                std::to_string(complex->sourceLine);
            diag.resolvedDecl = complex->signature;
            diag.betterDecl = trivial->signature;
            diag.message =
                "Coverage property divergence in " + className + ": " +
                complex->signature + " (body complexity: " +
                std::to_string(complex->bodyStmtCount) +
                " stmts, inline=" +
                (complex->isInlined ? "true" : "false") + ", constexpr=" +
                (complex->isConstexpr ? "true" : "false") + ") vs " +
                trivial->signature + " (body complexity: " +
                std::to_string(trivial->bodyStmtCount) +
                " stmts, inline=" +
                (trivial->isInlined ? "true" : "false") + ", constexpr=" +
                (trivial->isConstexpr ? "true" : "false") +
                "). The more complex method may receive different "
                "optimization treatment affecting coverage instrumentation.";
            diagnostics.push_back(std::move(diag));
          }
        }
      }
    }
  }
}

// --- runAnalysis ---

std::vector<Diagnostic>
runAnalysis(const clang::tooling::CompilationDatabase &compDb,
            const std::vector<std::string> &sourceFiles,
            bool enableCoverageDiag) {
  // Phase 1: Index all translation units.
  GlobalIndex index;
  {
    auto tool = giga_drill::makeClangTool(compDb, sourceFiles);
    IndexerActionFactory factory(index);
    tool.run(&factory);
  }

  // Phase 1.5: Coverage property analysis (index-only, no AST needed).
  std::vector<Diagnostic> diagnostics;
  if (enableCoverageDiag)
    analyzeCoverageProperties(index, diagnostics);

  // Phase 2: Analyze each translation unit against the global index.
  {
    auto tool = giga_drill::makeClangTool(compDb, sourceFiles);
    AnalyzerActionFactory factory(index, diagnostics);
    tool.run(&factory);
  }

  return diagnostics;
}

} // namespace giga_drill
