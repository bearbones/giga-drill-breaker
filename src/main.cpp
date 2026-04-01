#include "giga_drill/mugann/Analyzer.h"
#include "giga_drill/lagann/TransformPipeline.h"

#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

// ---------------------------------------------------------------------------
// Subcommands
// ---------------------------------------------------------------------------

static llvm::cl::SubCommand
    MugannCmd("mugann",
              "Analyze sources for fragile ADL/CTAD resolutions");

static llvm::cl::SubCommand
    LagannCmd("lagann",
              "Apply AST-based source transformations");

// ---------------------------------------------------------------------------
// mugann options
// ---------------------------------------------------------------------------

static llvm::cl::opt<std::string>
    MugannBuildPath("build-path",
                    llvm::cl::desc("Directory containing compile_commands.json"),
                    llvm::cl::value_desc("dir"),
                    llvm::cl::sub(MugannCmd));

static llvm::cl::list<std::string>
    MugannSourceFiles("source",
                      llvm::cl::desc("Source files to analyze"),
                      llvm::cl::value_desc("file"),
                      llvm::cl::OneOrMore,
                      llvm::cl::sub(MugannCmd));

static llvm::cl::opt<bool>
    MugannCoverageDiag("coverage-diag",
                       llvm::cl::desc("Enable coverage instrumentation diagnostics"),
                       llvm::cl::sub(MugannCmd));

// ---------------------------------------------------------------------------
// lagann options
// ---------------------------------------------------------------------------

static llvm::cl::opt<std::string>
    LagannRulesJson("rules-json",
                    llvm::cl::desc("JSON file with transform rules"),
                    llvm::cl::value_desc("file"),
                    llvm::cl::sub(LagannCmd));

static llvm::cl::opt<std::string>
    LagannBuildPath("build-path",
                    llvm::cl::desc("Directory containing compile_commands.json"),
                    llvm::cl::value_desc("dir"),
                    llvm::cl::sub(LagannCmd));

static llvm::cl::list<std::string>
    LagannSourceFiles("source",
                      llvm::cl::desc("Source files to transform"),
                      llvm::cl::value_desc("file"),
                      llvm::cl::OneOrMore,
                      llvm::cl::sub(LagannCmd));

static llvm::cl::opt<bool>
    LagannDryRun("dry-run",
                 llvm::cl::desc("Print replacements without applying them"),
                 llvm::cl::sub(LagannCmd));

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, const char **argv) {
  llvm::cl::ParseCommandLineOptions(
      argc, argv,
      "giga-drill-breaker: AST-based C++ analysis and transformation tool\n"
      "\nSubcommands:\n"
      "  mugann   Detect fragile ADL/CTAD resolution across translation units\n"
      "  lagann   Apply rule-driven AST matcher transformations\n");

  // ---- mugann ---------------------------------------------------------------
  if (MugannCmd) {
    if (MugannBuildPath.empty()) {
      llvm::errs() << "mugann: --build-path is required\n";
      return 1;
    }
    if (MugannSourceFiles.empty()) {
      llvm::errs() << "mugann: at least one --source file is required\n";
      return 1;
    }

    std::string dbError;
    auto compDb = clang::tooling::CompilationDatabase::loadFromDirectory(
        MugannBuildPath, dbError);
    if (!compDb) {
      llvm::errs() << "mugann: error loading compilation database from "
                   << MugannBuildPath << ": " << dbError << "\n";
      return 1;
    }

    std::vector<std::string> files(MugannSourceFiles.begin(),
                                   MugannSourceFiles.end());
    auto diagnostics =
        giga_drill::runAnalysis(*compDb, files, MugannCoverageDiag);

    if (diagnostics.empty()) {
      llvm::outs() << "mugann: no fragile ADL/CTAD resolutions found.\n";
      return 0;
    }

    for (const auto &diag : diagnostics)
      llvm::outs() << diag.callLocation << ": " << diag.message << "\n";

    return 0;
  }

  // ---- lagann ---------------------------------------------------------------
  if (LagannCmd) {
    if (LagannRulesJson.empty()) {
      llvm::errs() << "lagann: --rules-json is required\n";
      return 1;
    }
    if (LagannBuildPath.empty()) {
      llvm::errs() << "lagann: --build-path is required\n";
      return 1;
    }
    if (LagannSourceFiles.empty()) {
      llvm::errs() << "lagann: at least one --source file is required\n";
      return 1;
    }

    // TODO: Parse LagannRulesJson and populate pipeline passes.
    // For now, demonstrate that the tool links and runs.
    llvm::outs() << "lagann\n"
                 << "  rules:      " << LagannRulesJson << "\n"
                 << "  build-path: " << LagannBuildPath << "\n"
                 << "  sources:    " << LagannSourceFiles.size() << " file(s)\n"
                 << "  dry-run:    " << (LagannDryRun ? "yes" : "no") << "\n";

    giga_drill::TransformPipeline pipeline;
    std::vector<std::string> files(LagannSourceFiles.begin(),
                                   LagannSourceFiles.end());
    return pipeline.execute(LagannBuildPath, files, LagannDryRun);
  }

  llvm::errs() << "No subcommand specified. Use 'mugann' or 'lagann'.\n"
               << "Run with --help for usage information.\n";
  return 1;
}
