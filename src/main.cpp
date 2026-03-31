#include "giga_drill/MatcherEngine.h"
#include "giga_drill/TransformPipeline.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

static llvm::cl::opt<std::string>
    RulesJson("rules-json",
              llvm::cl::desc("JSON file with transform rules"),
              llvm::cl::value_desc("file"));

static llvm::cl::opt<std::string>
    BuildPath("build-path",
              llvm::cl::desc("Directory containing compile_commands.json"),
              llvm::cl::value_desc("dir"));

static llvm::cl::list<std::string>
    SourceFiles("source",
                llvm::cl::desc("Source files to transform"),
                llvm::cl::value_desc("file"),
                llvm::cl::OneOrMore);

static llvm::cl::opt<bool>
    DryRun("dry-run",
           llvm::cl::desc("Print replacements without applying them"));

int main(int argc, const char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv,
                                    "giga-drill-breaker: AST-based source "
                                    "transformation tool\n");

  if (RulesJson.empty()) {
    llvm::errs() << "Error: --rules-json is required\n";
    return 1;
  }

  if (BuildPath.empty()) {
    llvm::errs() << "Error: --build-path is required\n";
    return 1;
  }

  // TODO: Parse rules JSON file and populate pipeline.
  // For now, just demonstrate that the tool links and runs.
  llvm::outs() << "giga-drill-breaker\n"
               << "  rules:      " << RulesJson << "\n"
               << "  build-path: " << BuildPath << "\n"
               << "  sources:    " << SourceFiles.size() << " file(s)\n"
               << "  dry-run:    " << (DryRun ? "yes" : "no") << "\n";

  giga_drill::TransformPipeline pipeline;
  std::vector<std::string> files(SourceFiles.begin(), SourceFiles.end());
  return pipeline.execute(BuildPath, files, DryRun);
}
