# ContextBuilder Refactoring Plan

## Overview
This plan documents the refactoring of MemoryManager to extract context building logic into a separate ContextBuilder class.

## Completed Work

### 1. Created ContextBuilder Header File
- **File**: `include/inference/ContextBuilder.hpp` (CREATED)
- **Status**: Complete
- **Contents**:
  - ContextBuilder class declaration
  - Constructor: `explicit ContextBuilder(const MemoryManager* manager)`
  - Methods:
    - `BuildSystemPrompt(const std::string& user_prompt) const` - Build system prompt with memory status
    - `BuildStatementContext() const` - Build statement-based context
    - `UpdateSystemStatus(const std::string& context, int32_t current_round) const` - Update system status markers
    - `BuildNumberedContext() const` - Build numbered context for AI modification (legacy)
    - `BuildStatusBlock(const MemoryStatus&, const std::string&, int32_t)` - Static helper for status block
    - `GetCurrentTimestamp()` - Static helper for timestamp
  - Private helpers:
    - `FormatPercentage(int32_t, int32_t)` - Format memory percentages
    - `GetArchiveFiles() const` - Get archive file list

### 2. Updated MemoryManager Header
- **File**: `include/inference/MemoryManager.hpp` (MODIFIED)
- **Status**: Complete
- **Changes**:
  - Added `#include "inference/ContextBuilder.hpp"`
  - Added `ContextBuilder` class declaration before `MemoryManager` class
  - Added `friend class ContextBuilder;` declaration in MemoryManager

### 3. Created Empty ContextBuilder.cpp File
- **File**: `src/inference/ContextBuilder.cpp` (CREATED - EMPTY)
- **Status**: Needs implementation

## Work Remaining

### Task 1: Implement ContextBuilder Methods in ContextBuilder.cpp
**Location**: `src/inference/ContextBuilder.cpp`

**Methods to implement** (extracted from MemoryManager.cpp):

#### 1.1 `ContextBuilder::FormatPercentage()`
- **Source**: Inline logic from MemoryManager
- **Code**:
```cpp
std::string ContextBuilder::FormatPercentage(int32_t tokens, int32_t max_tokens)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d%%", max_tokens > 0 ? (100 * tokens / max_tokens) : 0);
    return std::string(buf);
}
```

#### 1.2 `ContextBuilder::GetArchiveFiles()`
- **Source**: Extracted from `MemoryManager::GetArchiveIndexInternal()` (lines 83-94)
- **Code**:
```cpp
std::vector<std::string> ContextBuilder::GetArchiveFiles() const
{
    std::vector<std::string> files;
    std::string archive_dir = m_manager->m_config.archive_path;
    if (!fs::exists(archive_dir)) return files;
    for (const auto& entry : fs::directory_iterator(archive_dir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".txt")
        {
            files.push_back(entry.path().filename().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}
```

#### 1.3 `ContextBuilder::BuildStatusBlock()`
- **Source**: Similar to `MemoryManager::BuildStatementContext()` status block (lines 884-893)
- **Code**:
```cpp
std::string ContextBuilder::BuildStatusBlock(
    const MemoryStatus& status,
    const std::string& current_time,
    int32_t current_round)
{
    std::stringstream ss;
    ss << "<system status>\n";
    ss << "IDENTITY:    " << status.identity_tokens << "/" << status.identity_max << " tokens ("
       << FormatPercentage(status.identity_tokens, status.identity_max) << ")\n";
    ss << "SESSION:     " << status.session_tokens << "/" << status.session_max << " tokens ("
       << FormatPercentage(status.session_tokens, status.session_max) << ")\n";
    ss << "CONVERSATION: " << status.conversation_tokens << "/" << status.conversation_max << " tokens ("
       << FormatPercentage(status.conversation_tokens, status.conversation_max) << ")\n";
    ss << "CURRENT TIME: " << current_time << "\n";
    ss << "ROUND: " << current_round << "/20\n";
    ss << "</system status>";
    return ss.str();
}
```

#### 1.4 `ContextBuilder::GetCurrentTimestamp()`
- **Source**: Extracted from `MemoryManager::GetCurrentTimestamp()` (lines 712-718)
- **Code**:
```cpp
std::string ContextBuilder::GetCurrentTimestamp()
{
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}
```

#### 1.5 `ContextBuilder::BuildSystemPrompt()`
- **Source**: Extracted from `MemoryManager::BuildSystemPrompt()` (lines 250-344)
- **Key logic**:
  1. Get memory status via `m_manager->GetMemoryStatusInternal()`
  2. Get archive files via `m_manager->GetArchiveIndexInternal()`
  3. Build system prompt with:
     - Current time
     - Memory structure status (identity, session, conversation, archive)
     - Archive file index
     - Capabilities documentation
     - Memory control guidelines
     - Recommendations
     - Numbered context (if warnings exist) via `BuildNumberedContext()`
     - User prompt
- **Note**: This method calls `BuildNumberedContext()` internally

#### 1.6 `ContextBuilder::BuildStatementContext()`
- **Source**: Extracted from `MemoryManager::BuildStatementContext()` (lines 842-896)
- **Key logic**:
  1. Get memory status
  2. Build context header
  3. Add statement entries
  4. Add memory control rules
  5. Add system status block via `BuildStatusBlock()`

#### 1.7 `ContextBuilder::UpdateSystemStatus()`
- **Source**: Extracted from `MemoryManager::UpdateSystemStatus()` (lines 898-931)
- **Key logic**:
  1. Find `<system status>` and `</system status>` markers
  2. Generate new status content
  3. Replace in-place

#### 1.8 `ContextBuilder::BuildNumberedContext()`
- **Source**: Extracted from `MemoryManager::BuildNumberedContext()` (lines 401-445)
- **Key logic**:
  1. Add numbered lines for identity
  2. Add numbered lines for session summary
  3. Add numbered lines for conversation

### Task 2: Update MemoryManager.cpp to Use ContextBuilder
**Location**: `src/inference/MemoryManager.cpp`

**Changes needed**:

#### 2.1 Add ContextBuilder member to MemoryManager
- Add `mutable ContextBuilder m_contextBuilder;` as a member of MemoryManager
- Initialize in constructor: `m_contextBuilder(this)`

#### 2.2 Update BuildSystemPrompt() to delegate
- Replace the full implementation with:
```cpp
std::string MemoryManager::BuildSystemPrompt(const std::string& user_prompt) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_contextBuilder.BuildSystemPrompt(user_prompt);
}
```

#### 2.3 Update BuildStatementContext() to delegate
- Replace the full implementation with:
```cpp
std::string MemoryManager::BuildStatementContext() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_contextBuilder.BuildStatementContext();
}
```

#### 2.4 Update UpdateSystemStatus() to delegate
- Replace the full implementation with:
```cpp
std::string MemoryManager::UpdateSystemStatus(const std::string& context, int32_t current_round) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_contextBuilder.UpdateSystemStatus(context, current_round);
}
```

#### 2.5 Update BuildNumberedContext() to delegate
- Replace the full implementation with:
```cpp
std::string MemoryManager::BuildNumberedContext() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_contextBuilder.BuildNumberedContext();
}
```

### Task 3: Update CMakeLists.txt
**Location**: `CMakeLists.txt`

**Changes needed**:
- Add `src/inference/ContextBuilder.cpp` to the source files list

### Task 4: Verify Compilation
- Build the project
- Fix any compilation errors
- Test context building functionality

## File Structure After Refactoring

```
include/inference/
    ContextBuilder.hpp      (NEW - ContextBuilder class declaration)
    MemoryManager.hpp       (MODIFIED - includes ContextBuilder.hpp, friend declaration)

src/inference/
    ContextBuilder.cpp      (NEW - ContextBuilder implementation)
    MemoryManager.cpp       (MODIFIED - delegates to ContextBuilder, reduced from ~1004 lines)

CMakeLists.txt              (MODIFIED - adds ContextBuilder.cpp)
```

## Important Notes

1. **Thread Safety**: MemoryManager methods still acquire locks before delegating to ContextBuilder
2. **Friend Access**: ContextBuilder is a friend of MemoryManager to access private members
3. **Internal Methods**: ContextBuilder uses `GetMemoryStatusInternal()` and `GetArchiveIndexInternal()` to avoid double-locking
4. **ContextBuilder.cpp requires these includes**:
   - `#include "inference/ContextBuilder.hpp"`
   - `#include <sstream>`
   - `#include <iomanip>`
   - `#include <ctime>`
   - `#include <algorithm>`
   - `#include <filesystem>`
5. **Namespace**: All code is in `FreeAI::Inference`

## Reminders for the Coder

- The ContextBuilder header file is already created at `include/inference/ContextBuilder.hpp`
- The ContextBuilder.cpp file exists but is empty at `src/inference/ContextBuilder.cpp`
- MemoryManager.hpp already has the friend declaration and ContextBuilder include
- The original MemoryManager.cpp has ~1004 lines; after refactoring it should be ~600 lines
- When extracting methods, preserve all original logic exactly
- Test compilation after each major change
- The `m_config.archive_path` member is accessed directly (MemoryManager is friend)
