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
  clangDriver
  clangFrontend
  clangSema
  clangSerialization
  clangRewrite
  clangToolingCore
  LLVMSupport
)

# Embed the Clang resource directory so the tool can find built-in headers
# (stdarg.h, etc.) when consuming compilation databases from other toolchains.
if(LLVM_FOUND)
  execute_process(
    COMMAND "${LLVM_TOOLS_BINARY_DIR}/clang" -print-resource-dir
    OUTPUT_VARIABLE _GIGA_DRILL_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(_GIGA_DRILL_RESOURCE_DIR)
    target_compile_definitions(giga_drill_lib PRIVATE
      "GIGA_DRILL_CLANG_RESOURCE_DIR=\"${_GIGA_DRILL_RESOURCE_DIR}\"")
  endif()
endif()

add_executable(giga-drill-breaker main.cpp)
target_link_libraries(giga-drill-breaker PRIVATE giga_drill_lib)
