//===-- MemoryManager.cpp -------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CoreStats.h"
#include "Memory.h"
#include "MemoryManager.h"

#include "klee/Expr.h"
#include "klee/Internal/Support/ErrorHandling.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"

#if defined(CRETE_CONFIG)
#include "crete-replayer/qemu_rt_info.h"
#endif // CRETE_CONFIG

#include <sys/mman.h>
using namespace klee;

namespace {
llvm::cl::opt<bool> DeterministicAllocation(
    "allocate-determ",
    llvm::cl::desc("Allocate memory deterministically(default=off)"),
    llvm::cl::init(false));

llvm::cl::opt<unsigned> DeterministicAllocationSize(
    "allocate-determ-size",
    llvm::cl::desc(
        "Preallocated memory for deterministic allocation in MB (default=100)"),
    llvm::cl::init(100));

llvm::cl::opt<bool>
    NullOnZeroMalloc("return-null-on-zero-malloc",
                     llvm::cl::desc("Returns NULL in case malloc(size) was "
                                    "called with size 0 (default=off)."),
                     llvm::cl::init(false));

llvm::cl::opt<unsigned> RedZoneSpace(
    "red-zone-space",
    llvm::cl::desc("Set the amount of free space between allocations. This is "
                   "important to detect out-of-bound accesses (default=10)."),
    llvm::cl::init(10));

llvm::cl::opt<unsigned long long> DeterministicStartAddress(
    "allocate-determ-start-address",
    llvm::cl::desc("Start address for deterministic allocation. Has to be page "
                   "aligned (default=0x7ff30000000)."),
    llvm::cl::init(0x7ff30000000));
}

/***/
MemoryManager::MemoryManager(ArrayCache *_arrayCache)
    : arrayCache(_arrayCache), deterministicSpace(0), nextFreeSlot(0),
      spaceSize(DeterministicAllocationSize.getValue() * 1024 * 1024) {
  if (DeterministicAllocation) {
    // Page boundary
    void *expectedAddress = (void *)DeterministicStartAddress.getValue();

    char *newSpace =
        (char *)mmap(expectedAddress, spaceSize, PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    if (newSpace == MAP_FAILED) {
      klee_error("Couldn't mmap() memory for deterministic allocations");
    }
    if (expectedAddress != newSpace && expectedAddress != 0) {
      klee_error("Could not allocate memory deterministically");
    }

    klee_message("Deterministic memory allocation starting from %p", newSpace);
    deterministicSpace = newSpace;
    nextFreeSlot = newSpace;
  }

#if defined(CRETE_CONFIG)
    next_alloc_address = 0;

    assert(!DeterministicAllocation && "[CRETE ERROR] DeterministicAllocation should be disabled.\n");
#endif
}

MemoryManager::~MemoryManager() {
  while (!objects.empty()) {
    MemoryObject *mo = *objects.begin();
    if (!mo->isFixed && !DeterministicAllocation)
      free((void *)mo->address);
    objects.erase(mo);
    delete mo;
  }

  if (DeterministicAllocation)
    munmap(deterministicSpace, spaceSize);
}

#if !defined(CRETE_CONFIG)
MemoryObject *MemoryManager::allocate(uint64_t size, bool isLocal,
                                      bool isGlobal,
                                      const llvm::Value *allocSite,
                                      size_t alignment) {
  if (size > 10 * 1024 * 1024)
    klee_warning_once(0, "Large alloc: %lu bytes.  KLEE may run out of memory.",
                      size);

  // Return NULL if size is zero, this is equal to error during allocation
  if (NullOnZeroMalloc && size == 0)
    return 0;

  if (!llvm::isPowerOf2_64(alignment)) {
    klee_warning("Only alignment of power of two is supported");
    return 0;
  }

  uint64_t address = 0;
  if (DeterministicAllocation) {

    address = llvm::RoundUpToAlignment((uint64_t)nextFreeSlot + alignment - 1,
                                       alignment);

    // Handle the case of 0-sized allocations as 1-byte allocations.
    // This way, we make sure we have this allocation between its own red zones
    size_t alloc_size = std::max(size, (uint64_t)1);
    if ((char *)address + alloc_size < deterministicSpace + spaceSize) {
      nextFreeSlot = (char *)address + alloc_size + RedZoneSpace;
    } else {
      klee_warning_once(
          0,
          "Couldn't allocate %lu bytes. Not enough deterministic space left.",
          size);
      address = 0;
    }
  } else {
    // Use malloc for the standard case
    if (alignment <= 8)
      address = (uint64_t)malloc(size);
    else {
      int res = posix_memalign((void **)&address, alignment, size);
      if (res < 0) {
        klee_warning("Allocating aligned memory failed.");
        address = 0;
      }
    }
  }

  if (!address)
    return 0;

  ++stats::allocations;
  MemoryObject *res = new MemoryObject(address, size, isLocal, isGlobal, false,
                                       allocSite, this);
  objects.insert(res);
  return res;
}
#else //CRETE_CONFIG
// UPGRADE: xxx alignment is new
MemoryObject *MemoryManager::allocate(uint64_t size, bool isLocal,
                                      bool isGlobal,
                                      const llvm::Value *allocSite,
                                      size_t alignment) {
  if (size > 10 * 1024 * 1024)
    klee_warning_once(0, "Large alloc: %lu bytes.  KLEE may run out of memory.",
                      size);

  // Return NULL if size is zero, this is equal to error during allocation
  if (NullOnZeroMalloc && size == 0)
    return 0;

  if (!llvm::isPowerOf2_64(alignment)) {
    klee_warning("Only alignment of power of two is supported");
    return 0;
  }

  assert(alignment <= 8 && "[CRETE UPGRADE ERROR] check whether it is safe to perform "
          "crete customized allocate when alignment is larger than 8\n");

  uint64_t address = get_next_address(size);

  ++stats::allocations;
  bool isFixed = true;
  MemoryObject *res = new MemoryObject(address, size, isLocal, isGlobal, isFixed,
                                       allocSite, this);

  objects.insert(res);
  return res;
}
#endif // CRETE_CONFIG

#if !defined(CRETE_CONFIG)
MemoryObject *MemoryManager::allocateFixed(uint64_t address, uint64_t size,
                                           const llvm::Value *allocSite)
#else
MemoryObject *MemoryManager::allocateFixed(uint64_t address, uint64_t size,
                                           const llvm::Value *allocSite,
                                           bool crete_call)
#endif
{
#ifndef NDEBUG
  for (objects_ty::iterator it = objects.begin(), ie = objects.end(); it != ie;
       ++it) {
    MemoryObject *mo = *it;
    if (address + size > mo->address && address < mo->address + mo->size)
    {
#if defined(CRETE_CONFIG)
        // Check on the validity of return is done by crete code;
        if(crete_call){
            cerr << "[CRETE ERROR] Trying to allocate an overlapping object: "
                    << "addr = 0x"<< hex << mo->address
                    << ", size = " << dec << mo->size << endl;
            return NULL;
        }
#endif // CRETE_CONFIG

        klee_error("Trying to allocate an overlapping object");
    }
  }
#endif

  ++stats::allocations;
  MemoryObject *res =
      new MemoryObject(address, size, false, true, true, allocSite, this);
  objects.insert(res);
  return res;
}

void MemoryManager::deallocate(const MemoryObject *mo) { assert(0); }

void MemoryManager::markFreed(MemoryObject *mo) {
  if (objects.find(mo) != objects.end()) {
    if (!mo->isFixed && !DeterministicAllocation)
      free((void *)mo->address);
    objects.erase(mo);
  }
}

size_t MemoryManager::getUsedDeterministicSize() {
  return nextFreeSlot - deterministicSpace;
}

#if defined(CRETE_CONFIG)
MemoryObject *MemoryManager::findObject(uint64_t address) const {
  for (objects_ty::iterator it = objects.begin(), ie = objects.end();
       it != ie; ++it) {
    MemoryObject *mo = *it;
    if (address == mo->address )
      return mo;
  }

  return 0;
}

std::vector<MemoryObject *> MemoryManager::findOverlapObjects(uint64_t address,
        uint64_t size) const {
    std::vector<MemoryObject *> overlapped_mos;

    for (objects_ty::iterator it = objects.begin(), ie = objects.end();
            it != ie; ++it) {
        MemoryObject *mo = *it;
        if (address+size > mo->address && address < mo->address+mo->size) {
            overlapped_mos.push_back(mo);
        }
    }

    return overlapped_mos;
}

bool MemoryManager::isOverlappedMO(uint64_t address, uint64_t size) const {
    for (objects_ty::iterator it = objects.begin(), ie = objects.end();
            it != ie; ++it) {
        MemoryObject *mo = *it;
        if (address+size > mo->address && address < mo->address+mo->size) {
            return true;
        }
    }

    return false;
}

uint64_t MemoryManager::get_next_address(uint64_t size) {
  if (next_alloc_address < KLEE_ALLOC_RANGE_LOW) {
    next_alloc_address = KLEE_ALLOC_RANGE_LOW;
  }

  assert(next_alloc_address + size <= KLEE_ALLOC_RANGE_HIGH && "[MemoryManager::get_next_address] allocate overflow.");

  uint64_t ret = next_alloc_address;
  next_alloc_address += size;

  return ret;
}
#endif // CRETE_CONFIG
