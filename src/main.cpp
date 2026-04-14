// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "giga_drill/mugann/Analyzer.h"
#include "giga_drill/mugann/DeadCodeAnalyzer.h"
#include "giga_drill/callgraph/CallGraphBuilder.h"
#include "giga_drill/callgraph/ControlFlowIndex.h"
#include "giga_drill/callgraph/ControlFlowOracle.h"
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

static llvm::cl::SubCommand
    CfqueryCmd("cfquery",
               "Query control flow and exception handling context");

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

static llvm::cl::opt<bool>
    MugannDeadCode("dead-code",
                   llvm::cl::desc("Enable dead code analysis via call graph"),
                   llvm::cl::sub(MugannCmd));

static llvm::cl::list<std::string>
    MugannEntryPoints("entry-point",
                      llvm::cl::desc("Entry point function names (default: main)"),
                      llvm::cl::value_desc("name"),
                      llvm::cl::sub(MugannCmd));

static llvm::cl::opt<bool>
    MugannWarnSameScore("warn-same-score",
                        llvm::cl::desc("Warn on ADL candidates that tie the "
                                       "resolved overload on every argument "
                                       "position"),
                        llvm::cl::sub(MugannCmd));

static llvm::cl::opt<bool>
    MugannModelConvertibility("model-convertibility",
                              llvm::cl::desc("Use indexed type relations "
                                             "(inheritance, converting ctors, "
                                             "conversion operators) to decide "
                                             "candidate viability"),
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
// cfquery options
// ---------------------------------------------------------------------------

static llvm::cl::opt<std::string>
    CfqueryBuildPath("build-path",
                     llvm::cl::desc("Directory containing compile_commands.json"),
                     llvm::cl::value_desc("dir"),
                     llvm::cl::sub(CfqueryCmd));

static llvm::cl::list<std::string>
    CfquerySourceFiles("source",
                       llvm::cl::desc("Source files to analyze"),
                       llvm::cl::value_desc("file"),
                       llvm::cl::OneOrMore,
                       llvm::cl::sub(CfqueryCmd));

static llvm::cl::list<std::string>
    CfqueryEntryPoints("entry-point",
                       llvm::cl::desc("Entry point function names (default: main)"),
                       llvm::cl::value_desc("name"),
                       llvm::cl::sub(CfqueryCmd));

enum CfqueryMode { CfqueryDump, CfqueryQuery };
static llvm::cl::opt<CfqueryMode>
    CfqueryModeOpt("mode",
                   llvm::cl::desc("Output mode"),
                   llvm::cl::values(
                       clEnumValN(CfqueryDump, "dump",
                                  "Dump full control flow index as JSON"),
                       clEnumValN(CfqueryQuery, "query",
                                  "Run a targeted query")),
                   llvm::cl::init(CfqueryDump),
                   llvm::cl::sub(CfqueryCmd));

enum CfqueryType {
  CfqExceptionProtection,
  CfqCallSiteContext,
  CfqAllPathContexts,
  CfqThrowPropagation,
  CfqNearestCatches
};
static llvm::cl::opt<CfqueryType>
    CfqueryQueryType("query-type",
                     llvm::cl::desc("Type of query to run (requires --mode query)"),
                     llvm::cl::values(
                         clEnumValN(CfqExceptionProtection,
                                    "exception-protection",
                                    "Is function always/sometimes/never under try/catch?"),
                         clEnumValN(CfqCallSiteContext,
                                    "call-site-context",
                                    "Exception context at a specific call site"),
                         clEnumValN(CfqAllPathContexts,
                                    "all-path-contexts",
                                    "All paths to function with exception context"),
                         clEnumValN(CfqThrowPropagation,
                                    "throw-propagation",
                                    "Is a thrown exception caught before unwinding?"),
                         clEnumValN(CfqNearestCatches,
                                    "nearest-catches",
                                    "Nearest try/catch on each path to function")),
                     llvm::cl::init(CfqExceptionProtection),
                     llvm::cl::sub(CfqueryCmd));

static llvm::cl::opt<std::string>
    CfqueryFunction("function",
                    llvm::cl::desc("Target function (qualified name)"),
                    llvm::cl::value_desc("name"),
                    llvm::cl::sub(CfqueryCmd));

static llvm::cl::opt<std::string>
    CfqueryCallSite("call-site",
                    llvm::cl::desc("Call site location (file:line:col)"),
                    llvm::cl::value_desc("location"),
                    llvm::cl::sub(CfqueryCmd));

static llvm::cl::opt<std::string>
    CfqueryExceptionType("exception-type",
                         llvm::cl::desc("Exception type for protection queries"),
                         llvm::cl::value_desc("type"),
                         llvm::cl::sub(CfqueryCmd));

static llvm::cl::opt<unsigned>
    CfqueryMaxPaths("max-paths",
                    llvm::cl::desc("Maximum number of paths to enumerate (default: 100)"),
                    llvm::cl::init(100),
                    llvm::cl::sub(CfqueryCmd));

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, const char **argv) {
  llvm::cl::ParseCommandLineOptions(
      argc, argv,
      "giga-drill-breaker: AST-based C++ analysis and transformation tool\n"
      "\nSubcommands:\n"
      "  mugann   Detect fragile ADL/CTAD resolution across translation units\n"
      "  lagann   Apply rule-driven AST matcher transformations\n"
      "  cfquery  Query control flow and exception handling context\n");

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
    giga_drill::AnalysisOptions opts;
    opts.enableCoverageDiag = MugannCoverageDiag;
    opts.warnSameScore = MugannWarnSameScore;
    opts.modelConvertibility = MugannModelConvertibility;
    auto diagnostics = giga_drill::runAnalysis(*compDb, files, opts);

    // Dead code analysis.
    if (MugannDeadCode) {
      auto graph = giga_drill::buildCallGraph(*compDb, files);

      std::vector<std::string> entryPoints(MugannEntryPoints.begin(),
                                           MugannEntryPoints.end());
      if (entryPoints.empty())
        entryPoints.push_back("main");

      giga_drill::DeadCodeAnalyzer analyzer(graph, entryPoints);
      analyzer.analyzePessimistic();
      analyzer.analyzeOptimistic();

      auto deadDiags = analyzer.getDiagnostics();
      for (const auto &diag : deadDiags) {
        diagnostics.push_back(diag);
      }
    }

    if (diagnostics.empty()) {
      llvm::outs() << "mugann: no issues found.\n";
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

  // ---- cfquery --------------------------------------------------------------
  if (CfqueryCmd) {
    if (CfqueryBuildPath.empty()) {
      llvm::errs() << "cfquery: --build-path is required\n";
      return 1;
    }
    if (CfquerySourceFiles.empty()) {
      llvm::errs() << "cfquery: at least one --source file is required\n";
      return 1;
    }

    std::string dbError;
    auto compDb = clang::tooling::CompilationDatabase::loadFromDirectory(
        CfqueryBuildPath, dbError);
    if (!compDb) {
      llvm::errs() << "cfquery: error loading compilation database from "
                   << CfqueryBuildPath << ": " << dbError << "\n";
      return 1;
    }

    std::vector<std::string> files(CfquerySourceFiles.begin(),
                                   CfquerySourceFiles.end());

    // Phase 1+2: Build call graph.
    auto graph = giga_drill::buildCallGraph(*compDb, files);

    // Phase 3: Build control flow index.
    auto cfIndex = giga_drill::buildControlFlowIndex(*compDb, files, graph);

    // Dump mode: serialize the full index as JSON.
    if (CfqueryModeOpt == CfqueryDump) {
      llvm::outs() << giga_drill::ControlFlowOracle::dumpIndexToJson(cfIndex);
      return 0;
    }

    // Query mode: run a specific query.
    giga_drill::ControlFlowOracle oracle(graph, cfIndex);

    std::vector<std::string> entryPoints(CfqueryEntryPoints.begin(),
                                         CfqueryEntryPoints.end());
    if (entryPoints.empty())
      entryPoints.push_back("main");

    switch (CfqueryQueryType) {
    case CfqCallSiteContext: {
      if (CfqueryCallSite.empty()) {
        llvm::errs() << "cfquery: --call-site is required for "
                        "call-site-context query\n";
        return 1;
      }
      auto info = oracle.queryCallSite(CfqueryCallSite);
      // Simple JSON output for call site info.
      llvm::outs() << "{\n"
                   << "  \"callSite\": \"" << info.callSite << "\",\n"
                   << "  \"caller\": \"" << info.caller << "\",\n"
                   << "  \"callee\": \"" << info.callee << "\",\n"
                   << "  \"isUnderTryCatch\": "
                   << (info.isUnderTryCatch ? "true" : "false") << ",\n"
                   << "  \"wouldTerminateIfThrows\": "
                   << (info.wouldTerminateIfThrows ? "true" : "false") << ",\n"
                   << "  \"enclosingScopeCount\": "
                   << info.enclosingScopes.size() << ",\n"
                   << "  \"enclosingGuardCount\": "
                   << info.enclosingGuards.size() << "\n"
                   << "}\n";
      return 0;
    }

    case CfqExceptionProtection: {
      if (CfqueryFunction.empty()) {
        llvm::errs() << "cfquery: --function is required for "
                        "exception-protection query\n";
        return 1;
      }
      auto result = oracle.queryExceptionProtection(
          CfqueryFunction, CfqueryExceptionType, entryPoints);
      llvm::outs() << giga_drill::ControlFlowOracle::toJson(
          result, "exception-protection", CfqueryFunction,
          CfqueryExceptionType);
      return 0;
    }

    case CfqAllPathContexts: {
      if (CfqueryFunction.empty()) {
        llvm::errs() << "cfquery: --function is required for "
                        "all-path-contexts query\n";
        return 1;
      }
      auto result = oracle.queryExceptionProtection(
          CfqueryFunction, CfqueryExceptionType, entryPoints);
      llvm::outs() << giga_drill::ControlFlowOracle::toJson(
          result, "all-path-contexts", CfqueryFunction,
          CfqueryExceptionType);
      return 0;
    }

    case CfqThrowPropagation: {
      if (CfqueryFunction.empty()) {
        llvm::errs() << "cfquery: --function is required for "
                        "throw-propagation query\n";
        return 1;
      }
      auto result = oracle.queryThrowPropagation(
          CfqueryFunction, CfqueryExceptionType, entryPoints);
      llvm::outs() << giga_drill::ControlFlowOracle::toJson(
          result, "throw-propagation", CfqueryFunction,
          CfqueryExceptionType);
      return 0;
    }

    case CfqNearestCatches: {
      if (CfqueryFunction.empty()) {
        llvm::errs() << "cfquery: --function is required for "
                        "nearest-catches query\n";
        return 1;
      }
      auto catches = oracle.queryNearestCatches(CfqueryFunction);
      llvm::outs() << "{\n"
                   << "  \"query\": \"nearest-catches\",\n"
                   << "  \"function\": \"" << CfqueryFunction.getValue()
                   << "\",\n"
                   << "  \"results\": [\n";
      for (size_t i = 0; i < catches.size(); ++i) {
        const auto &c = catches[i];
        llvm::outs() << "    {\n"
                     << "      \"framesFromTarget\": " << c.framesFromTarget
                     << ",\n"
                     << "      \"tryLocation\": \""
                     << c.scope.tryLocation << "\",\n"
                     << "      \"enclosingFunction\": \""
                     << c.scope.enclosingFunction << "\",\n"
                     << "      \"pathSegment\": [";
        for (size_t j = 0; j < c.pathSegment.size(); ++j) {
          llvm::outs() << "\"" << c.pathSegment[j] << "\"";
          if (j + 1 < c.pathSegment.size())
            llvm::outs() << ", ";
        }
        llvm::outs() << "]\n"
                     << "    }";
        if (i + 1 < catches.size())
          llvm::outs() << ",";
        llvm::outs() << "\n";
      }
      llvm::outs() << "  ]\n}\n";
      return 0;
    }
    }

    return 0;
  }

  llvm::errs() << "No subcommand specified. Use 'mugann', 'lagann', or "
                  "'cfquery'.\n"
               << "Run with --help for usage information.\n";
  return 1;
}
