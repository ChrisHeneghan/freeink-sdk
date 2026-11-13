#include "OpdsFeedWindow.h"

#include <MemoryManager.h>
#include <OpdsLog.h>

#include <algorithm>
#include <utility>

namespace {
using namespace OpdsLimits;
constexpr size_t ALLOCATION_OVERHEAD = 32;
constexpr size_t ENTRY_TEXT_BYTES = 2 * (MAX_TITLE_CHARS + MAX_AUTHOR_CHARS + MAX_ID_CHARS + 3 * MAX_HREF_CHARS +
                                         MAX_DESCRIPTION_CHARS + 2 * MAX_TITLE_CHARS + 9 * ALLOCATION_OVERHEAD);
constexpr size_t METADATA_BYTES = 9 * (MAX_PAGE_URL_CHARS + ALLOCATION_OVERHEAD) + MAX_TITLE_CHARS;
// Parser buffers, entry scratch and the UI's metadata copies coexist with rows.
constexpr size_t WORKSPACE_BYTES = 8192 + 2 * ENTRY_TEXT_BYTES + 2 * METADATA_BYTES;
constexpr size_t ACCOUNT_BYTES = 3 * (MAX_PAGE_URL_CHARS + 2 * MAX_TITLE_CHARS + 3 * ALLOCATION_OVERHEAD);

bool affordable(size_t bytes, size_t block) {
  const auto& memory = freeink::MemoryManager::instance();
  for (auto pool : {freeink::MemPool::Default, freeink::MemPool::Internal}) {
    if (memory.freeBytes(pool) <= WORKSPACE_BYTES + bytes || memory.largestFreeBlock(pool) < block) {
      LOG_ERR("OPDS", "Feed allocation rejected: bytes=%u block=%u free=%u largest=%u", static_cast<unsigned>(bytes),
              static_cast<unsigned>(block), static_cast<unsigned>(memory.freeBytes(pool)),
              static_cast<unsigned>(memory.largestFreeBlock(pool)));
      return false;
    }
  }
  return true;
}

size_t textBytes(const OpdsEntry& entry) {
  size_t bytes = 0;
  for (const auto* text : {&entry.title, &entry.author, &entry.href, &entry.id, &entry.detail, &entry.selfHref,
                           &entry.description, &entry.coverHref})
    bytes += text->capacity() + 1 + ALLOCATION_OVERHEAD;
  // Group headings may be attached only after their entries have been retained.
  return bytes + std::max(entry.heading.capacity() + 1, 2 * MAX_TITLE_CHARS + 1) + ALLOCATION_OVERHEAD;
}
}  // namespace

bool OpdsFeedWindow::hasWorkspace(size_t block) { return affordable(PRIMARY_BYTES + SUPPLEMENTARY_BYTES, block); }

size_t OpdsFeedWindow::reserve(std::vector<OpdsEntry>& entries) {
  if (request.bytesPerRow > PRIMARY_BYTES / MAX_WINDOW_ENTRIES ||
      MAX_WINDOW_ENTRIES * (sizeof(OpdsEntry) + request.bytesPerRow + ALLOCATION_OVERHEAD) >= PRIMARY_BYTES ||
      !hasWorkspace(MAX_WINDOW_ENTRIES * sizeof(OpdsEntry))) {
    failed = true;
    LOG_ERR("OPDS", "Cannot reserve bounded feed rows");
    return 0;
  }
  if (request.mode == OpdsWindowMode::Forward) entries.reserve(MAX_WINDOW_ENTRIES);
  retainedBytes = MAX_WINDOW_ENTRIES * (sizeof(OpdsEntry) + request.bytesPerRow + ALLOCATION_OVERHEAD);
  LOG_INF("OPDS", "Feed offset %u, capacity %u, byte budget %u", static_cast<unsigned>(request.start),
          static_cast<unsigned>(MAX_WINDOW_ENTRIES), static_cast<unsigned>(PRIMARY_BYTES));
  return MAX_WINDOW_ENTRIES;
}

bool OpdsFeedWindow::retain(OpdsEntry& entry, std::vector<OpdsEntry>& entries) {
  const size_t index = seen++;
  if (request.mode == OpdsWindowMode::Previous) {
    if (index >= request.start || failed) return false;
    const size_t bytes = textBytes(entry);
    const size_t baseBytes = MAX_WINDOW_ENTRIES * (sizeof(OpdsEntry) + request.bytesPerRow + ALLOCATION_OVERHEAD);
    if (bytes > PRIMARY_BYTES - baseBytes) {
      LOG_ERR("OPDS", "One feed entry exceeds the window budget");
      failed = true;
      return false;
    }
    if (countedEntries == MAX_WINDOW_ENTRIES || bytes > PRIMARY_BYTES - retainedBytes) {
      first = index;
      retainedBytes = baseBytes;
      countedEntries = 0;
    }
    retainedBytes += bytes;
    ++countedEntries;
    next = index + 1;
    return false;
  }
  if (index < request.start || more || failed) return false;
  const size_t bytes = textBytes(entry);
  if (index >= request.end || entries.size() >= MAX_WINDOW_ENTRIES || bytes > PRIMARY_BYTES - retainedBytes) {
    more = true;
    limited = index < request.end;
    next = index;
    if (entries.empty()) {
      LOG_ERR("OPDS", "One feed entry exceeds the window budget");
      failed = true;
    }
    return false;
  }
  if (!affordable(0, 2 * MAX_HREF_CHARS + ALLOCATION_OVERHEAD)) {
    failed = true;
    return false;
  }
  retainedBytes += bytes;
  entries.push_back(std::move(entry));
  next = index + 1;
  return true;
}

bool OpdsFeedWindow::retainFacet(OpdsEntry& entry, std::vector<OpdsEntry>& facets) {
  if (request.mode == OpdsWindowMode::Previous) return false;
  if (facets.capacity() == 0) {
    supplementaryBytes =
        (MAX_FACET_ENTRIES + 3) * (sizeof(OpdsEntry) + request.bytesPerRow + ALLOCATION_OVERHEAD) + ACCOUNT_BYTES;
    if (supplementaryBytes >= SUPPLEMENTARY_BYTES) return false;
    if (!affordable(MAX_FACET_ENTRIES * sizeof(OpdsEntry), MAX_FACET_ENTRIES * sizeof(OpdsEntry))) {
      failed = true;
      return false;
    }
    facets.reserve(MAX_FACET_ENTRIES);
  }
  const size_t bytes = textBytes(entry);
  if (facets.size() >= MAX_FACET_ENTRIES || bytes > SUPPLEMENTARY_BYTES - supplementaryBytes) return false;
  if (!affordable(0, 2 * MAX_HREF_CHARS + ALLOCATION_OVERHEAD)) {
    failed = true;
    return false;
  }
  supplementaryBytes += bytes;
  facets.push_back(std::move(entry));
  return true;
}

void OpdsLimits::reserveFacetEntries(std::vector<OpdsEntry>& facets) {
  const size_t bytes = MAX_FACET_ENTRIES * sizeof(OpdsEntry);
  if (affordable(bytes, bytes)) facets.reserve(MAX_FACET_ENTRIES);
}

size_t OpdsLimits::reserveFeedEntries(std::vector<OpdsEntry>& entries) {
  OpdsFeedWindow window;
  return window.reserve(entries);
}
