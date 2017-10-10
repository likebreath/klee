//===-- MemoryManager.h -----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_MEMORYMANAGER_H
#define KLEE_MEMORYMANAGER_H

#include <set>
#include <stdint.h>

#if defined(CRETE_CONFIG)

#define PAGE_ALIGN_BITS     (12)
#define PAGE_SIZE           (1<<PAGE_ALIGN_BITS)
#define PAGE_ADDRESS_MASK   (0xffffffffffffffffLL << PAGE_ALIGN_BITS)
#define PAGE_OFFSET_MASK    (~PAGE_ADDRESS_MASK)

#endif //defined(CRETE_CONFIG)

namespace llvm {
class Value;
}

namespace klee {
class MemoryObject;
class ArrayCache;

class MemoryManager {
private:
  typedef std::set<MemoryObject *> objects_ty;
  objects_ty objects;
  ArrayCache *const arrayCache;

  char *deterministicSpace;
  char *nextFreeSlot;
  size_t spaceSize;

public:
  MemoryManager(ArrayCache *arrayCache);
  ~MemoryManager();

  /**
   * Returns memory object which contains a handle to real virtual process
   * memory.
   */
  MemoryObject *allocate(uint64_t size, bool isLocal, bool isGlobal,
                         const llvm::Value *allocSite, size_t alignment);
#if !defined(CRETE_CONFIG)
  MemoryObject *allocateFixed(uint64_t address, uint64_t size,
                              const llvm::Value *allocSite);
#else
  MemoryObject *allocateFixed(uint64_t address, uint64_t size,
                              const llvm::Value *allocSite,
                              bool crete_call = false,
                              bool skip_check = false);
#endif
  void deallocate(const MemoryObject *mo);
  void markFreed(MemoryObject *mo);
  ArrayCache *getArrayCache() const { return arrayCache; }

  /*
   * Returns the size used by deterministic allocation in bytes
   */
  size_t getUsedDeterministicSize();

#if defined(CRETE_CONFIG)
  private:
    uint64_t next_alloc_address;

    uint64_t get_next_address(uint64_t size);

    //  <static_addr, MemoryObject (with dynamic address)>
    map<uint64_t, MemoryObject *> m_dyn_addr_map;

  public:
    bool find_dynamic_page_mo(uint64_t static_addr, MemoryObject *&ret_mo);
#endif
};

} // End klee namespace

#endif
