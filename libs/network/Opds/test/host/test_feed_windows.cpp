#include <MemoryManager.h>
#include <OpdsFeedParser.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

namespace {
union alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__) AllocationHeader {
  struct {
    size_t size, offset, regionSize;
  } record;
  std::max_align_t alignment;
};
}  // namespace
void* operator new(size_t size) {
  auto* header = static_cast<AllocationHeader*>(std::malloc(sizeof(AllocationHeader) + size));
  if (!header) throw std::bad_alloc();
  const size_t regionSize = (size + 32 + 15) & ~size_t(15);
  const size_t offset = FragmentedHeap::enabled ? FragmentedHeap::allocate(regionSize) : FragmentedHeap::NO_BLOCK;
  if (FragmentedHeap::enabled && offset == FragmentedHeap::NO_BLOCK) {
    std::free(header);
    throw std::bad_alloc();
  }
  header->record = {size, offset, regionSize};
  freeink::testAllocated += size;
  return header + 1;
}
void operator delete(void* value) noexcept {
  if (!value) return;
  auto* header = static_cast<AllocationHeader*>(value) - 1;
  freeink::testAllocated -= header->record.size;
  if (header->record.offset != FragmentedHeap::NO_BLOCK)
    FragmentedHeap::release(header->record.offset, header->record.regionSize);
  std::free(header);
}
void operator delete(void* value, size_t) noexcept { ::operator delete(value); }
void operator delete[](void* value, size_t) noexcept { ::operator delete(value); }
void* operator new(size_t size, const std::nothrow_t&) noexcept {
  try {
    return ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}
void operator delete(void* value, const std::nothrow_t&) noexcept { ::operator delete(value); }
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete[](void* value) noexcept { ::operator delete(value); }

std::string atomEntry(int id) {
  return "<entry><title>Book " + std::to_string(id) + "</title><id>" + std::to_string(id) + "</id><summary>" +
         std::string(500, 'x') +
         "</summary><link rel=\"http://opds-spec.org/acquisition\" type=\"application/epub+zip\" href=\"/book/" +
         std::to_string(id) + "\"/></entry>";
}
std::string feed(bool json, int count) {
  std::string out = json ? "{\"publications\":[" : "<feed>";
  for (int i = 0; i < count; ++i) {
    if (json) {
      if (i) out += ',';
      out += "{\"metadata\":{\"title\":\"Book " + std::to_string(i) + "\",\"identifier\":\"" + std::to_string(i) +
             "\",\"description\":\"" + std::string(500, 'x') +
             "\"},\"links\":[{\"rel\":\"http://opds-spec.org/acquisition\",\"type\":\"application/"
             "epub+zip\",\"href\":\"/book/" +
             std::to_string(i) + "\"}]}";
    } else {
      out += "<entry><title>Invalid</title></entry>";
      out += atomEntry(i);
    }
  }
  return out +
         (json ? "],\"links\":[{\"rel\":\"next\",\"href\":\"/next\"}]}" : "<link rel=\"next\" href=\"/next\"/></feed>");
}
void write(OpdsFeedParser& parser, const std::string& text) {
  for (size_t n = 0; n < text.size(); n += 127)
    parser.write(reinterpret_cast<const uint8_t*>(text.data() + n), std::min(size_t(127), text.size() - n));
  parser.flush();
}
int main() {
  // Mixed field sizes and invalid entries must yield the same backward boundaries.
  for (bool json : {false, true}) {
    std::string text = json ? "{\"publications\":[" : "<feed>";
    for (int i = 0; i < 80; ++i) {
      const std::string title(i % 5 ? 4 : 160, 't');
      const std::string description(i % 5 ? 20 : 500, 'd');
      const std::string author(i % 5 ? 1 : 120, 'a');
      const std::string id(i % 5 ? 1 : 128, 'i');
      const std::string href = "/book/" + std::to_string(i) + std::string(i % 5 ? 0 : 720, 'u');
      if (json) {
        if (i) text += ',';
        if (i % 7 == 0)
          text += "{\"metadata\":{\"title\":\"" + std::string(160, 'x') + "\",\"description\":\"" +
                  std::string(500, 'z') + "\"}},";
        text += "{\"metadata\":{\"title\":\"" + title + "\",\"description\":\"" + description + "\",\"author\":\"" +
                author + "\",\"identifier\":\"" + id +
                "\"},\"links\":[{\"rel\":\"acquisition\",\"type\":\"application/epub+zip\",\"href\":\"" + href +
                "\"}]}";
      } else {
        if (i % 7 == 0)
          text += "<entry><title>" + std::string(160, 'x') + "</title><summary>" + std::string(500, 'z') +
                  "</summary></entry>";
        text += "<entry><title>" + title + "</title><summary>" + description + "</summary><author><name>" + author +
                "</name></author><id>" + id + "</id><link type=\"application/atom+xml\" href=\"" + href +
                "\"/></entry>";
      }
    }
    text += json ? "]}" : "</feed>";
    size_t start = 0;
    std::vector<std::pair<size_t, size_t>> ranges;
    do {
      OpdsFeedParser parser;
      parser.setWindow({start, SIZE_MAX, 128});
      write(parser, text);
      assert(!parser.error());
      ranges.push_back({start, parser.nextEntryOffset()});
      if (!parser.hasMoreEntries()) break;
      start = parser.nextEntryOffset();
    } while (true);
    size_t before = SIZE_MAX;
    for (auto range = ranges.rbegin(); range != ranges.rend(); ++range) {
      OpdsFeedParser parser;
      parser.setWindow({before, SIZE_MAX, 128, OpdsWindowMode::Previous});
      write(parser, text);
      assert(!parser.error());
      assert(parser.firstEntryOffset() == range->first && parser.nextEntryOffset() == range->second);
      before = range->first;
    }
  }
  // Reconstruct earlier windows without retaining an offset list or entries.
  for (bool json : {false, true}) {
    const auto text = feed(json, 180);
    std::vector<std::pair<size_t, size_t>> ranges;
    size_t offset = 0;
    do {
      OpdsFeedParser parser;
      parser.setWindow({offset, SIZE_MAX, 128});
      write(parser, text);
      assert(!parser.error());
      ranges.push_back({offset, parser.nextEntryOffset()});
      if (!parser.hasMoreEntries()) break;
      offset = parser.nextEntryOffset();
    } while (true);
    size_t before = SIZE_MAX;
    for (auto range = ranges.rbegin(); range != ranges.rend(); ++range) {
      OpdsFeedParser parser;
      parser.setWindow({before, SIZE_MAX, 128, OpdsWindowMode::Previous});
      write(parser, text);
      assert(!parser.error() && !parser.memoryError() && !parser.hasEntries());
      assert(parser.firstEntryOffset() == range->first && parser.nextEntryOffset() == range->second);
      parser.reset();
      parser.setWindow({range->first, range->second, 128});
      write(parser, text);
      auto entries = parser.takeEntries();
      assert(!parser.error() && entries.size() == range->second - range->first);
      for (size_t i = 0; i < entries.size(); ++i)
        assert(entries[i].href == "/book/" + std::to_string(range->first + i));
      before = range->first;
    }
    auto bad = text;
    bad.pop_back();
    OpdsFeedParser malformed;
    malformed.setWindow({50, SIZE_MAX, 128, OpdsWindowMode::Previous});
    write(malformed, bad);
    assert(malformed.error());
  }
  for (bool json : {false, true}) {
    for (int count : {0, 1, 16, 50, 130}) {
      const auto text = feed(json, count);
      std::vector<std::string> expected;
      std::vector<size_t> boundaries;
      for (size_t block : {size_t(1000000), size_t(32768), size_t(20000)}) {
        freeink::testFree = freeink::testInternalFree = 1000000;
        freeink::testBlock = freeink::testInternalBlock = block;
        std::vector<std::string> all;
        std::vector<size_t> offsets;
        size_t offset = 0;
        do {
          OpdsFeedParser parser;
          parser.setWindow({offset, SIZE_MAX, 128});
          write(parser, text);
          assert(!parser.error() && !parser.memoryError());
          assert(parser.getNextPageUrl() == "/next");
          auto entries = parser.takeEntries();
          for (const auto& e : entries) all.push_back(e.href);
          offsets.push_back(parser.nextEntryOffset());
          if (!parser.hasMoreEntries()) break;
          assert(!entries.empty() && parser.nextEntryOffset() > offset);
          offset = parser.nextEntryOffset();
        } while (true);
        assert(all.size() == static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) assert(all[i] == "/book/" + std::to_string(i));
        if (expected.empty()) {
          expected = all;
          boundaries = offsets;
        } else
          assert(all == expected && offsets == boundaries);
      }
    }
    freeink::testBlock = freeink::testInternalBlock = 1000000;
    OpdsFeedParser bounded;
    bounded.setWindow({7, 11, 128});
    write(bounded, feed(json, 50));
    auto entries = bounded.takeEntries();
    assert(!bounded.error() && entries.size() == 4 && bounded.nextEntryOffset() == 11);
    assert(entries.front().href == "/book/7" && entries.back().href == "/book/10");
    for (bool internal : {false, true}) {
      if (internal)
        freeink::testInternalBlock = 1;
      else
        freeink::testFree = 1;
      OpdsFeedParser low;
      write(low, feed(json, 1));
      assert(low.memoryError());
      freeink::testFree = freeink::testInternalBlock = 1000000;
    }
    auto bad = feed(json, 50);
    bad.pop_back();
    OpdsFeedParser malformed;
    malformed.setWindow({0, 1, 128});
    write(malformed, bad);
    assert(malformed.error());
    // Account C++ allocations and demand the same boundaries with less free RAM.
    const auto text = feed(json, 50);
    size_t count = 0;
    for (size_t free : {size_t(1000000), size_t(85000)}) {
      freeink::testAccounting = true;
      freeink::testBaseline = freeink::testAllocated;
      freeink::testFree = freeink::testInternalFree = free;
      {
        OpdsFeedParser parser;
        parser.setWindow({0, SIZE_MAX, 128});
        write(parser, text);
        assert(!parser.error() && !parser.memoryError() && parser.hasMoreEntries());
        const auto size = parser.takeEntries().size();
        assert(size > 0 && size <= 16);
        if (count)
          assert(count == size);
        else
          count = size;
      }
      freeink::testAccounting = false;
    }
    freeink::testFree = freeink::testInternalFree = 1000000;
  }
  // Long supported fields hit the byte budget before the entry ceiling.
  for (bool json : {false, true}) {
    std::string text = json ? "{\"publications\":[" : "<feed>";
    std::vector<std::string> expected;
    for (int i = 0; i < 40; ++i) {
      const std::string href = "/book/" + std::to_string(i) + "/" + std::string(730, 'u');
      expected.push_back(href);
      if (json) {
        if (i) text += ',';
        text += "{\"metadata\":{\"title\":\"" + std::string(160, 't') + "\",\"author\":\"" + std::string(120, 'a') +
                "\",\"identifier\":\"" + std::string(128, 'i') + "\",\"description\":\"" + std::string(500, 'd') +
                "\"},\"images\":[{\"href\":\"" + href +
                "\"}],\"links\":[{\"rel\":\"acquisition\",\"type\":\"application/epub+zip\",\"href\":\"" + href +
                "\"},{\"rel\":\"self\",\"type\":\"application/opds-publication+json\",\"href\":\"" + href + "\"}]}";
      } else {
        text += "<entry><title>" + std::string(160, 't') + "</title><author><name>" + std::string(120, 'a') +
                "</name></author><id>" + std::string(128, 'i') + "</id><summary>" + std::string(500, 'd') +
                "</summary><link rel=\"http://opds-spec.org/image\" href=\"" + href +
                "\"/><link rel=\"http://opds-spec.org/acquisition\" type=\"application/epub+zip\" href=\"" + href +
                "\"/></entry>";
      }
    }
    text +=
        json ? "],\"links\":[{\"rel\":\"next\",\"href\":\"/late\"}]}" : "<link rel=\"next\" href=\"/late\"/></feed>";
    size_t offset = 0;
    std::vector<std::string> all;
    do {
      OpdsFeedParser parser;
      parser.setWindow({offset, SIZE_MAX, 128});
      write(parser, text);
      assert(!parser.error() && !parser.memoryError() && parser.getNextPageUrl() == "/late");
      auto entries = parser.takeEntries();
      assert(!entries.empty() && entries.size() < 16);
      for (const auto& entry : entries) {
        assert(entry.description.size() == 500 && entry.author.size() == 120);
        all.push_back(entry.href);
      }
      if (!parser.hasMoreEntries()) break;
      assert(parser.nextEntryOffset() > offset);
      offset = parser.nextEntryOffset();
    } while (true);
    assert(all == expected);
  }
  // Fragmentation alone must not demand one aggregate workspace-sized block.
  const auto fragmentedFeed = feed(true, 50);
  FragmentedHeap::reset(32768);
  FragmentedHeap::enabled = true;
  {
    OpdsFeedParser parser;
    write(parser, fragmentedFeed);
    assert(!parser.error() && parser.hasEntries() && parser.hasMoreEntries());
  }
  FragmentedHeap::enabled = false;
  // Untrusted row-size requests must fail without arithmetic overflow.
  {
    OpdsFeedParser parser;
    parser.setWindow({0, SIZE_MAX, SIZE_MAX});
    write(parser, feed(false, 1));
    assert(parser.memoryError());
  }
  // Group preview limits and synthetic links do not shift continuation offsets.
  std::string group =
      R"({"navigation":[{"title":"G","href":"/g"},{"title":"Other","href":"/other"}],"groups":[{"metadata":{"title":"G"},"links":[{"rel":"self","href":"/g"}],"publications":[)";
  for (int i = 0; i < 9; ++i) {
    if (i) group += ',';
    group +=
        "{\"metadata\":{\"title\":\"P\"},\"links\":[{\"rel\":\"acquisition\",\"type\":\"application/"
        "epub+zip\",\"href\":\"/publication/" +
        std::to_string(i) + "\"}]}";
  }
  group += R"(]}],"facets":[{"metadata":{"title":"Sort"},"links":[{"title":"Title","href":"/sort"}]}]})";
  {
    OpdsFeedParser parser;
    write(parser, group);
    assert(!parser.error());
    auto entries = parser.takeEntries();
    assert(entries.size() == 7 && entries.front().heading == "G" && entries.back().href == "/other");
    assert(parser.takeFacetEntries().size() == 1);
  }
  {
    std::vector<std::string> publications;
    size_t offset = 0;
    do {
      OpdsFeedParser parser;
      parser.setWindow({offset, offset + 2, 128});
      write(parser, group);
      assert(!parser.error());
      for (const auto& entry : parser.takeEntries())
        if (entry.type == OpdsEntryType::BOOK) publications.push_back(entry.href);
      if (!parser.hasMoreEntries()) break;
      assert(parser.nextEntryOffset() > offset);
      offset = parser.nextEntryOffset();
    } while (true);
    assert(publications.size() == 5);
    for (int i = 0; i < 5; ++i) assert(publications[i] == "/publication/" + std::to_string(i));
  }
  {
    OpdsFeedParser preceding;
    preceding.setWindow({SIZE_MAX, SIZE_MAX, 128, OpdsWindowMode::Previous});
    write(preceding, group);
    assert(!preceding.error() && !preceding.hasEntries() && preceding.takeFacetEntries().empty());
    const auto first = preceding.firstEntryOffset();
    const auto end = preceding.nextEntryOffset();
    preceding.reset();
    preceding.setWindow({first, end, 128});
    write(preceding, group);
    auto entries = preceding.takeEntries();
    assert(!preceding.error() && entries.size() == 7 && entries.front().heading == "G");
    assert(preceding.takeFacetEntries().size() == 1);
  }

  puts("OPDS windows: deterministic retention, continuation, groups, memory and validation passed");
}
