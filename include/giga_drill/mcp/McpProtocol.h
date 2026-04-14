#pragma once

#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <optional>
#include <string>

namespace giga_drill {

// JSON-RPC 2.0 error codes.
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;

/// A parsed JSON-RPC 2.0 request.
struct McpRequest {
  llvm::json::Value id;   // number, string, or null for notifications
  std::string method;
  llvm::json::Object params;

  bool isNotification() const;
};

/// Read one Content-Length framed JSON-RPC request from an input stream.
/// Returns std::nullopt on EOF or unrecoverable framing error.
/// On JSON parse error, writes a JSON-RPC error response and continues.
std::optional<McpRequest> readRequest(FILE *in, llvm::raw_ostream &errLog);

/// Write a JSON-RPC 2.0 success response to stdout.
void writeResult(const llvm::json::Value &id, llvm::json::Value result);

/// Write a JSON-RPC 2.0 error response to stdout.
void writeError(const llvm::json::Value &id, int code, llvm::StringRef message);

/// Write a raw JSON-RPC message (used for notifications from server).
void writeMessage(llvm::json::Value message);

} // namespace giga_drill
