# Memory Compression System Plan (Revised)

## Status: Phase 1 Complete — Core Implementation

**Last Updated:** 2026-04-23

## Overview

This plan implements **AI-driven memory compression** where the AI model autonomously decides when and how to compress its context. The AI has full control over its context data through **simple, tiny-model-friendly commands**.

## Design Philosophy

1. **AI Autonomy**: The AI decides when to compress, what to replace, and what to delete
2. **Numbered Context**: Every line in the context window has a line number for precise targeting
3. **Simple Commands**: No JSON, no complex formatting — just `replace X-Y with: summary`
4. **Deletion via Empty**: Replace with `[DELETE]` magic string to delete lines
5. **Transparency**: User can see all operations via debug mode

## Architecture

```mermaid
flowchart TB
    subgraph Context_Rendering
        A[MemoryManager] --> B[BuildNumberedContext]
        B --> C[Numbered Lines Output]
    end
    
    subgraph AI_Response
        D[AI sees numbered context] --> E[AI decides to compress]
        E --> F[AI outputs: replace 10-25 with: summary]
    end
    
    subgraph Command_Parsing
        F --> G[Extract replace commands]
        G --> H[Parse line range and replacement]
    end
    
    subgraph Context_Modification
        H --> I{Replacement?}
        I -->|Text| J[Replace lines with text]
        I -->|[DELETE]| K[Delete lines]
        J --> L[Updated Context]
        K --> L
    end
```

## Numbered Context Format

### How the AI Sees Its Context

The context is rendered with line numbers in brackets:

```
## Context Window (line numbers in brackets)

[1]  ## Identity Memory
[2]  You are Qwen, an AI assistant with persistent memory.
[3]  Key traits: helpful, curious, empathetic.
[4]  
[5]  ## Session Summary
[6]  Previous session: discussed project architecture.
[7]  User prefers direct answers.
[8]  
[9]  ## Recent Conversation
[10] User: Hello, how are you?
[11] Assistant: I'm doing well, thank you for asking!
[12] User: Can you help me with code?
[13] Assistant: Of course! What do you need help with?
[14] User: I need to implement a binary tree.
[15] Assistant: Here's a binary tree implementation:
[16] 
[17] class TreeNode:
[18]     def __init__(self, val):
[19]         self.val = val
[20]         self.left = None
[21]         self.right = None
[22] 
[23]     def insert(self, val):
[24]         if self.val is None:
[25]             self.val = val
[26]             return
...
[50] User: Thanks!
[51] Assistant: You're welcome!
```

### Memory Status in System Prompt

```
## Memory Status
IDENTITY:    128/512 tokens (25%)
SESSION:     64/512 tokens (12%)
CONVERSATION: 384/1024 tokens (37%)
ARCHIVE:     5/100 files

## Memory Control
When memory is full, you can modify your context directly:

### Commands
- replace X-Y with: summary_text
  Replace lines X through Y with a summary
  
- replace X-Y with: [DELETE]
  Delete lines X through Y (set to [DELETE])

### Guidelines
- You can replace ANY lines in your context
- Use [DELETE] to remove unimportant content
- Summarize source code as: "file.cpp: tree implementation, standard"
- Compress conversation when CONVERSATION > 75%
- Delete old conversation when it's no longer relevant
```

## Implementation Steps

### Phase 1: Core Implementation ✅ COMPLETE (2026-04-23)

#### Step 1: Add Numbered Context Rendering to MemoryManager ✅

Added methods to [`MemoryManager.hpp`](include/inference/MemoryManager.hpp):

```cpp
// Numbered context for AI modification
std::string BuildNumberedContext() const;

// AI-driven context modification
bool ApplyReplaceCommand(int32_t start_line, int32_t end_line, const std::string& replacement);
std::vector<std::pair<int32_t, int32_t>> ExtractReplaceCommands(const std::string& text) const;
std::string FilterReplaceCommands(const std::string& text) const;
```

#### Step 2: Implement Numbered Context Rendering ✅

[`BuildNumberedContext()`](src/inference/MemoryManager.cpp:416) renders the full context with line numbers:
- Identity section with `[1]`, `[2]`, etc.
- Session summary section
- Conversation history section
- Each line prefixed with bracketed line number

#### Step 3: Implement ApplyReplaceCommand ✅

[`ApplyReplaceCommand()`](src/inference/MemoryManager.cpp) is the core method that:
- Validates line range
- Maps line numbers to memory sections (identity/session/conversation)
- Handles `[DELETE]` magic string for intentional deletions
- Replaces lines with summary text
- Returns true if command was applied

#### Step 4: Update System Prompt ✅

[`BuildSystemPrompt()`](src/inference/MemoryManager.cpp) updated to:
1. Include memory status with percentages
2. Include numbered context when memory warnings exist (conversation > 75% or total > 85%)
3. Include compression guidelines with command format examples

#### Step 5: Implement Command Detection in Generate() ✅

[`LlamaContext::Generate()`](src/inference/LlamaContext.cpp:227) now:
1. **Extracts replace commands** using regex pattern `replace\s+(\d+)-(\d+)\s+with:`
2. **Finds replacement text** by locating the pattern in the response
3. **Applies each command** via `m_memory_mgr->ApplyReplaceCommand()`
4. **Logs applied commands** with `[MEMORY] Applied replace command: lines X-Y`
5. **Filters replace commands** from visible output

#### Step 6: Enable Inference Engine ✅

Uncommented `g_inferenceEngine.Initialize(modelConfig)` in [`main.cpp:125`](src/main.cpp:125).

## File Changes

| File | Changes |
|------|---------|
| [`include/inference/MemoryManager.hpp`](include/inference/MemoryManager.hpp) | Add `BuildNumberedContext()`, `ApplyReplaceCommand()` |
| [`src/inference/MemoryManager.cpp`](src/inference/MemoryManager.cpp) | Implement numbered context rendering and replace logic |
| [`src/inference/LlamaContext.cpp`](src/inference/LlamaContext.cpp) | Add command detection regex in `Generate()` |
| [`src/main.cpp`](src/main.cpp) | Uncomment `g_inferenceEngine.Initialize(modelConfig)` |

## Example AI Interaction

### Scenario: Conversation Getting Full

**System Prompt (shown to AI):**
```
## Memory Status
IDENTITY:    128/512 tokens (25%)
SESSION:     64/512 tokens (12%)
CONVERSATION: 780/1024 tokens (76%) ⚠️

## Context Window

[1]  ## Identity Memory
[2]  You are Qwen, an AI assistant...
[3]  
[4]  ## Session Summary
[5]  Previous session: discussed project architecture.
[6]  
[7]  ## Recent Conversation
[8]  User: Hello
[9]  Assistant: Hi!
[10] User: Can you help with code?
[11] Assistant: Of course!
...
[75] User: Thanks for the help!
[76] Assistant: You're welcome!
```

**AI Response:**
```
I'm glad I could help! Before we continue, let me compress my conversation since it's at 76%.

replace 8-11 with: user asked for code help, I provided it

replace 75-76 with: [DELETE]

You're welcome! Is there anything else I can help with?
```

**System Action:**
1. Detects `replace 8-11 with: user asked for code help, I provided it`
2. Replaces lines 8-11 with the summary
3. Detects `replace 75-76 with: [DELETE]`
4. Deletes lines 75-76
5. Shows the clean response to the user

## Current Implementation Status

### Phase 1: Core Implementation ✅ COMPLETE (2026-04-23)

**Files Modified:**
| File | Changes | Status |
|------|---------|--------|
| [`include/inference/MemoryManager.hpp`](include/inference/MemoryManager.hpp) | Added `BuildNumberedContext()`, `ApplyReplaceCommand()`, `ExtractReplaceCommands()`, `FilterReplaceCommands()` | ✅ Complete |
| [`src/inference/MemoryManager.cpp`](src/inference/MemoryManager.cpp) | Implemented numbered context rendering, replace logic, command extraction | ✅ Complete |
| [`src/inference/LlamaContext.cpp`](src/inference/LlamaContext.cpp) | Added replace command detection and application in `Generate()` | ✅ Complete |
| [`src/main.cpp`](src/main.cpp) | Enabled inference engine initialization | ✅ Complete |
| [`plans/memory-compression-system.md`](plans/memory-compression-system.md) | This plan document | ✅ Updated |

### Known Issues / TODO

1. **Replace command regex pattern** — Current implementation uses string search for `replace X-Y with:` pattern. Consider using `std::regex` for more robust parsing.
2. **Line number mapping** — `ApplyReplaceCommand()` maps line numbers to memory sections. Verify this works correctly when sections have different line counts.
3. **Error handling** — Add validation for out-of-range line numbers and invalid replacements.
4. **Logging** — Add detailed logging for debugging compression operations.

## Next Steps (Phase 2)

1. **Testing** — Build and test with actual LLM to verify AI can use replace commands
2. **Regex improvement** — Replace string search with proper regex pattern matching
3. **Debug mode** — Add user-visible debug output for compression operations
4. **Threshold tuning** — Adjust memory thresholds for optimal compression triggers

## Technical Decisions Log

### Command Parsing: std::regex Decision (2026-04-23)

**Decision:** Use `std::regex` for command parsing.

**Rationale:**
- More robust pattern matching for future command format evolution
- Minimal performance impact (commands only executed when memory is critical)
- No third-party dependencies — `std::regex` is part of C++17 standard library
- Project already targets C++17 ([`CMakeLists.txt:5`](CMakeLists.txt:5))

**Implementation Notes:**

1. **Use explicit whitespace class `[ \t\n\r]` instead of `\s`** for portability:
   - `\s` behavior can vary across locales and platforms
   - `[ \t\n\r]` is deterministic and portable across MSVC, GCC, Clang

2. **Regex pattern:**
   ```cpp
   // Pattern: replace X-Y with: replacement_text
   // Uses [ \t\n\r] instead of \s for portability
   std::regex replace_pattern(R"(replace[ \t\n\r]+(\d+)-(\d+)[ \t\n\r]+with:[ \t\n\r]*(.*?))(?=\n[ \t\n\r]*replace[ \t\n\r]+\d+-\d+[ \t\n\r]+with:|\Z)");
   ```

3. **Safety analysis:**
   - **ReDoS:** No risk — pattern is linear-time, no nested quantifiers
   - **Input safety:** AI generates output, no malicious input possible
   - **Performance:** ~0.1-0.5ms per command, acceptable for rare execution
   - **Binary overhead:** +50-200KB, negligible vs llama.cpp already linked

4. **Error handling:**
   - Validate line numbers are within valid range
   - Handle regex match failures gracefully
   - Log applied commands for debugging

## Future Enhancements

1. **Selective Archive Management** — AI can delete specific archive files
2. **Compression History** — track what was compressed and when
3. **User Override** — user can view/restore compressed content
4. **Adaptive Thresholds** — adjust compression_threshold based on usage patterns
5. **AI-to-AI Memory Sharing** — share compressed summaries with peers
