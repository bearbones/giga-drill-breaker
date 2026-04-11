#ifndef GIGA_DRILL_COMPAT_TOOL_ADJUSTERS_H
#define GIGA_DRILL_COMPAT_TOOL_ADJUSTERS_H

#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include <algorithm>
#include <string>
#include <vector>

namespace giga_drill {

/// Strip compiler flags that are incompatible with our LibTooling build.
/// This lets us consume compilation databases produced by toolchains whose
/// Clang version or configuration differs from ours (e.g. hermetic Xcode
/// builds that use -gmodules).
inline clang::tooling::ArgumentsAdjuster getStripIncompatibleFlagsAdjuster() {
  return [](const clang::tooling::CommandLineArguments &args,
            llvm::StringRef /*filename*/) {
    // Flags to remove outright.
    static const char *const StripFlags[] = {
        "-gmodules",
        "-fmodules",
        "-fcxx-modules",
        "-Werror",
    };
    // Flags whose *next* argument should also be removed.
    static const char *const StripWithNext[] = {
        "-fmodule-file",
        "-fmodules-cache-path",
        "-include-pch",
    };

    clang::tooling::CommandLineArguments filtered;
    filtered.reserve(args.size());
    bool skipNext = false;
    for (size_t i = 0; i < args.size(); ++i) {
      if (skipNext) {
        skipNext = false;
        continue;
      }
      bool skip = false;
      for (const auto *f : StripFlags) {
        if (args[i] == f) {
          skip = true;
          break;
        }
      }
      if (!skip) {
        for (const auto *f : StripWithNext) {
          if (args[i] == f) {
            skip = true;
            ++i; // skip next arg too
            break;
          }
        }
      }
      // Strip -include args that reference PCH files (CMake-style PCH).
      // These use "-include/path/to/pch" (joined) or "-include /path/to/pch".
      if (!skip && (args[i] == "-Xarch_arm64" || args[i] == "-Xarch_x86_64")) {
        // Check if next arg is a PCH include.
        if (i + 1 < args.size() &&
            args[i + 1].find("-include") == 0 &&
            args[i + 1].find("pch") != std::string::npos) {
          skip = true;
          ++i; // skip the -include arg too
        }
      }
      if (!skip && args[i].find("-include") == 0 &&
          args[i].find("pch") != std::string::npos) {
        skip = true;
        // If it's just "-include" (separate), skip next arg too.
        if (args[i] == "-include")
          ++i;
      }
      if (!skip)
        filtered.push_back(args[i]);
    }
    return filtered;
  };
}

/// Inject -resource-dir pointing to this tool's Clang resource directory.
/// This ensures the tool's built-in headers (stdarg.h, etc.) are found even
/// when consuming compilation databases from a different toolchain.
inline clang::tooling::ArgumentsAdjuster getResourceDirAdjuster() {
  return [](const clang::tooling::CommandLineArguments &args,
            llvm::StringRef /*filename*/) {
    // Check if -resource-dir is already set.
    for (const auto &a : args)
      if (a.find("-resource-dir") == 0)
        return args;
#ifdef GIGA_DRILL_CLANG_RESOURCE_DIR
    auto result = args;
    result.push_back("-resource-dir=" GIGA_DRILL_CLANG_RESOURCE_DIR);
    return result;
#else
    return args;
#endif
  };
}

/// Create a ClangTool with standard argument adjusters applied.
inline clang::tooling::ClangTool
makeClangTool(const clang::tooling::CompilationDatabase &compDb,
              const std::vector<std::string> &files) {
  clang::tooling::ClangTool tool(compDb, files);
  tool.appendArgumentsAdjuster(getStripIncompatibleFlagsAdjuster());
  tool.appendArgumentsAdjuster(
      clang::tooling::getClangStripDependencyFileAdjuster());
  tool.appendArgumentsAdjuster(getResourceDirAdjuster());
  return tool;
}

} // namespace giga_drill

#endif // GIGA_DRILL_COMPAT_TOOL_ADJUSTERS_H
