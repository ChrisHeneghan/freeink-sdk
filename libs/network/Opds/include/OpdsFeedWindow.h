#pragma once
#include <cstdint>

#include "OpdsEntry.h"

enum class OpdsWindowMode { Forward, Previous };

struct OpdsWindowRequest {
  size_t start = 0;
  size_t end = SIZE_MAX;
  size_t bytesPerRow = 0;
  // Previous discovers the last complete range before start, without retaining entries.
  OpdsWindowMode mode = OpdsWindowMode::Forward;
};

// Counts valid source entries independently of display rows and retention limits.
class OpdsFeedWindow {
 public:
  explicit OpdsFeedWindow(OpdsWindowRequest request = {})
      : request(request), first(request.mode == OpdsWindowMode::Forward ? request.start : 0), next(first) {}
  size_t reserve(std::vector<OpdsEntry>& entries);
  bool retain(OpdsEntry& entry, std::vector<OpdsEntry>& entries);
  bool wantsDetails() const {
    switch (request.mode) {
      case OpdsWindowMode::Forward:
        return seen >= request.start && seen < request.end && !more;
      case OpdsWindowMode::Previous:
        return seen < request.start;
    }
    return false;
  }
  bool hasMore() const { return more; }
  size_t nextOffset() const { return next; }
  size_t firstOffset() const { return first; }
  bool memoryLimited() const { return limited; }
  bool memoryError() const { return failed; }
  bool retainFacet(OpdsEntry& entry, std::vector<OpdsEntry>& facets);
  static bool hasWorkspace(size_t block = 2048);
  static constexpr size_t PRIMARY_BYTES = 16 * 1024;
  static constexpr size_t SUPPLEMENTARY_BYTES = 16 * 1024;
  static constexpr size_t MAX_WINDOW_ENTRIES = 16;

 private:
  OpdsWindowRequest request;
  size_t seen = 0;
  size_t first = 0;
  size_t next = 0;
  size_t retainedBytes = 0;
  size_t supplementaryBytes = 0;
  size_t countedEntries = 0;
  bool more = false;
  bool limited = false;
  bool failed = false;
};
