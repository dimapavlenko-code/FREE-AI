# ContextBuilder Refactoring - Remaining Work Plan

## Status Overview

The ContextBuilder refactoring is **approximately 80% complete**. The ContextBuilder class has been fully declared and implemented, but MemoryManager has not yet been updated to delegate to it.

### Completed
- [`ContextBuilder.hpp`](include/inference/ContextBuilder.hpp) - Full class declaration
- [`ContextBuilder.cpp`](src/inference/ContextBuilder.cpp) - All 8 methods implemented
- [`MemoryManager.hpp`](include/inference/MemoryManager.hpp) - Friend declaration, ContextBuilder member, constructor init
- [`CMakeLists.txt`](CMakeLists.txt) - ContextBuilder.cpp already listed

### Remaining (4 tasks)

---

## Task 1: Remove Inline ContextBuilder Class from MemoryManager.hpp

**File**: [`include/inference/MemoryManager.hpp`](include/inference/MemoryManager.hpp)

**Problem**: Lines 50-84 contain an inline `ContextBuilder` class definition that duplicates the standalone [`ContextBuilder.hpp`](include/inference/ContextBuilder.hpp). This causes issues since ContextBuilder now has its own .cpp file.

**Current content (lines 50-84)**:
```cpp
// =====================================================
// Context Builder - Extracted from MemoryManager
// =====================================================

class ContextBuilder {
public:
    ContextBuilder(const MemoryManager* manager) : m_manager(manager) {}

    // Build system prompt with memory status and context
    std::string BuildSystemPrompt(const std::string& user_prompt) const;

    // Build statement-based context with system status
    std::string BuildStatementContext() const;

    // Update system status markers in context string
    std::string UpdateSystemStatus(const std::string& context, int32_t current_round) const;

    // Build numbered context for AI modification (legacy)
    std::string BuildNumberedContext() const;

    // Build system status XML block
    static std::string BuildStatusBlock(
        const MemoryStatus& status,
        const std::string& current_time,
        int32_t current_round);

private:
    const MemoryManager* m_manager;

    // Helper: format memory status percentages
    static std::string FormatPercentage(int32_t tokens, int32_t max_tokens);

    // Helper: get archive file list
    std::vector<std::string> GetArchiveFiles();
};
```

**Action**: Delete lines 50-84 (the entire inline ContextBuilder class definition).

**Result**: The header will rely on `#include "inference/ContextBuilder.hpp"` (already present on line 3) for the ContextBuilder declaration.

---

## Task 2: Refactor MemoryManager::BuildSystemPrompt()

**File**: [`src/inference/MemoryManager.cpp`](src/inference/MemoryManager.cpp)

**Current**: Lines 251-345 contain full implementation (~95 lines)

**Replace with**:
```cpp
std::string MemoryManager::BuildSystemPrompt(const std::string& user_prompt) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_contextBuilder.BuildSystemPrompt(user_prompt);
}
```

**Note**: The lock is retained in MemoryManager to preserve thread safety semantics. ContextBuilder accesses memory via `m_manager->GetMemoryStatusInternal()` and `m_manager->GetArchiveIndexInternal()` which don't acquire additional locks.

---

## Task 3: Refactor MemoryManager::BuildStatementContext()

**File**: [`src/inference/MemoryManager.cpp`](src/inference/MemoryManager.cpp)

**Current**: Lines 843-897 contain full implementation (~55 lines)

**Replace with**:
```cpp
std::string MemoryManager::BuildStatementContext() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_contextBuilder.BuildStatementContext();
}
```

---

## Task 4: Refactor MemoryManager::UpdateSystemStatus()

**File**: [`src/inference/MemoryManager.cpp`](src/inference/MemoryManager.cpp)

**Current**: Lines 899-932 contain full implementation (~34 lines)

**Replace with**:
```cpp
std::string MemoryManager::UpdateSystemStatus(const std::string& context, int32_t current_round) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_contextBuilder.UpdateSystemStatus(context, current_round);
}
```

---

## Task 5: Refactor MemoryManager::BuildNumberedContext()

**File**: [`src/inference/MemoryManager.cpp`](src/inference/MemoryManager.cpp)

**Current**: Lines 402-446 contain full implementation (~45 lines)

**Replace with**:
```cpp
std::string MemoryManager::BuildNumberedContext() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_contextBuilder.BuildNumberedContext();
}
```

---

## File Changes Summary

| File | Changes | Lines Removed | Lines Added |
|------|---------|---------------|-------------|
| `include/inference/MemoryManager.hpp` | Remove inline ContextBuilder | ~35 | 0 |
| `src/inference/MemoryManager.cpp` | Delegate 4 methods | ~229 | ~16 |
| **Total** | | **~264** | **~16** |

**Net result**: MemoryManager.cpp reduced from ~1004 lines to ~756 lines (25% reduction)

---

## Verification Steps

After implementation:
1. Build the project
2. Verify no compilation errors
3. Verify no linker errors (ContextBuilder symbols should resolve)
4. Test context building functionality

---

## Important Notes

1. **Thread Safety**: MemoryManager methods acquire `m_mutex` before delegating to ContextBuilder
2. **Internal Methods**: ContextBuilder uses `GetMemoryStatusInternal()` and `GetArchiveIndexInternal()` to avoid double-locking
3. **Friend Access**: ContextBuilder is friend of MemoryManager to access private members directly
4. **No API Changes**: Public interface of MemoryManager remains unchanged
5. **ContextBuilder.cpp already complete**: No changes needed to ContextBuilder.cpp

---

## Implementation Order

1. **Task 1** - Remove inline ContextBuilder from header (do first to avoid duplicate class errors)
2. **Task 2** - Refactor BuildSystemPrompt()
3. **Task 3** - Refactor BuildStatementContext()
4. **Task 4** - Refactor UpdateSystemStatus()
5. **Task 5** - Refactor BuildNumberedContext()

Each task should be verified by building after completion.
