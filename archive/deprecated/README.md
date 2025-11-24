# Deprecated Components Archive

This directory contains obsolete code components that have been superseded by newer architecture versions.

---

## Contents

### SlotAllocator.hpp.v2.x
**Original Location:** `source/runtime/inc/SlotAllocator.hpp`  
**Deprecated Date:** 2025-01-21  
**Reason:** Superseded by v3.1 zero-conflict slot mapping design  

**Old Design (v2.x):**
- Hash-based slot allocation (FNV1a/CRC32)
- Collision resolution (linear/quadratic probing)
- Complex allocation logic with O(n) worst-case

**New Design (v3.1):**
- Zero-conflict direct mapping: `slot = service_id & 1023`
- Implemented as inline static methods in `SharedMemoryRegistry.hpp`
- O(1) constant-time slot calculation
- No hash collisions possible

**Migration Guide:**
```cpp
// OLD (v2.x) - Do not use
SlotAllocator allocator;
auto slot = allocator.ComputeServiceHash(service_name, instance_id);
auto result = allocator.AllocateSlot(service_id, instance_id);

// NEW (v3.1) - Current implementation
#include "SharedMemoryRegistry.hpp"
uint32_t slot = SharedMemoryRegistry::CalculateSlot(service_id);
RegistryType reg = SharedMemoryRegistry::SelectRegistry(service_id);
```

**References:**
- Architecture: `doc/architecture/SERVICE_DISCOVERY_ARCHITECTURE.md` v3.1
- Implementation: `source/registry/inc/SharedMemoryRegistry.hpp` lines 470-500
- Assessment: `doc/planning/CODE_IMPLEMENTATION_ASSESSMENT.md`

---

**Archive Maintenance:**
- Files in this directory are **NOT** compiled or linked
- Kept for historical reference only
- May be deleted after v3.1 stabilization (Q2 2025)
