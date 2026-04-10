add_library(giga_drill_lib
  mugann/GlobalIndex.cpp
  mugann/Indexer.cpp
  mugann/Analyzer.cpp
  mugann/DeadCodeAnalyzer.cpp
  lagann/MatcherEngine.cpp
  lagann/TransformPipeline.cpp
  callgraph/CallGraph.cpp
  callgraph/CallGraphBuilder.cpp
  callgraph/ControlFlowIndex.cpp
  callgraph/ControlFlowContextVisitor.cpp
  callgraph/ControlFlowOracle.cpp
)

target_include_directories(giga_drill_lib PUBLIC
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(giga_drill_lib PUBLIC
  clangTooling
  clangDynamicASTMatchers
  clangASTMatchers
  clangAST
  clangBasic
  clangFrontend
  clangSema
  clangSerialization
  clangRewrite
  clangToolingCore
  LLVMSupport
)

add_executable(giga-drill-breaker main.cpp)
target_link_libraries(giga-drill-breaker PRIVATE giga_drill_lib)
