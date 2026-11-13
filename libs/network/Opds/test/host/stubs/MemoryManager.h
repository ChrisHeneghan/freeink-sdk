#pragma once
#include <cstddef>
namespace FragmentedHeap {
struct Block {
  size_t offset = 0;
  size_t size = 0;
};
inline Block blocks[512];
inline bool enabled = false;
inline constexpr size_t NO_BLOCK = static_cast<size_t>(-1);
inline void reset(size_t largest) {
  for (auto& block : blocks) block = {};
  for (size_t i = 0; i < 128; ++i) blocks[i] = {i * 1024, 512};
  blocks[128] = {1 << 20, largest};
}
inline size_t freeBytes() {
  size_t result = 0;
  for (const auto& block : blocks) result += block.size;
  return result;
}
inline size_t largestBlock() {
  size_t result = 0;
  for (const auto& block : blocks)
    if (block.size > result) result = block.size;
  return result;
}
inline size_t allocate(size_t size) {
  for (auto& block : blocks) {
    if (block.size < size) continue;
    const size_t offset = block.offset;
    block.offset += size;
    block.size -= size;
    return offset;
  }
  return NO_BLOCK;
}
inline void release(size_t offset, size_t size) {
  for (auto& block : blocks) {
    if (block.size != 0) continue;
    block = {offset, size};
    bool changed;
    do {
      changed = false;
      for (auto& other : blocks) {
        if (&other == &block || other.size == 0) continue;
        if (block.offset + block.size == other.offset) {
          block.size += other.size;
          other = {};
          changed = true;
        } else if (other.offset + other.size == block.offset) {
          block.offset = other.offset;
          block.size += other.size;
          other = {};
          changed = true;
        }
      }
    } while (changed);
    return;
  }
  __builtin_trap();
}
}  // namespace FragmentedHeap

namespace freeink {
enum class MemPool { Internal, Psram, Default };
inline size_t testAllocated = 0, testBaseline = 0;
inline bool testAccounting = false;
inline size_t testFree = 1000000, testBlock = 1000000;
inline size_t testInternalFree = 1000000, testInternalBlock = 1000000;
class MemoryManager {
 public:
  static MemoryManager& instance() {
    static MemoryManager manager;
    return manager;
  }
  size_t freeBytes(MemPool pool = MemPool::Default) const {
    if (FragmentedHeap::enabled) return FragmentedHeap::freeBytes();
    const size_t available = pool == MemPool::Internal ? testInternalFree : testFree;
    const size_t used = testAccounting && testAllocated > testBaseline ? testAllocated - testBaseline : 0;
    return used < available ? available - used : 0;
  }
  size_t largestFreeBlock(MemPool pool = MemPool::Default) const {
    if (FragmentedHeap::enabled) return FragmentedHeap::largestBlock();
    const size_t block = pool == MemPool::Internal ? testInternalBlock : testBlock;
    return block < freeBytes(pool) ? block : freeBytes(pool);
  }
};
}  // namespace freeink
