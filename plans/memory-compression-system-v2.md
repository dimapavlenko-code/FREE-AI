# Memory Compression System — Statement-Based Approach (v2)

**Created:** 2026-04-24  
**Status:** Design Phase  
**Replaces:** [`memory-compression-system.md`](memory-compression-system.md) (line-numbered approach)

## Overview

This plan implements **AI-driven memory compression** using **statement-based context** instead of line-based context. Each statement (user input, AI output, tool report) is a single identifiable entity with a unique ID and timestamp.

## Design Philosophy

1. **AI Autonomy**: The AI decides when to compress, what to replace, and what to delete
2. **Statement-Based**: Each conversation turn is a single statement with unique ID
3. **Timestamps**: Every statement has a timestamp for time-based operations
4. **Simple Commands**: No JSON, no complex formatting — just `compress S1-001 to S1-005`
5. **Automatic Timestamp Management**: Model only knows current time (injected at end of each input)
6. **Clean Context**: No noise from line numbers, just semantic units
7. **Tool Reports**: Tool execution results replace tool calls in context

## Statement ID Format

Format: `S{session}-{sequence}` where session is current session number and sequence is zero-padded to 3 digits.

Examples: `S1-001`, `S1-002`, `S1-003`

## Statement Types

| Type | Description | Example |
|------|-------------|---------|
| USER | User input | `S1-005 [2026-04-24 14:30:00] USER: Hello, how are you?` |
| ASSISTANT | AI generated output | `S1-006 [2026-04-24 14:30:01] ASSISTANT: I am doing well!` |
| TOOL_CALL | Tool execution request | `S1-007 [2026-04-24 14:30:02] TOOL_CALL: compress S1-001 to S1-003` |
| TOOL_REPORT | Tool execution result | `S1-008 [2026-04-24 14:30:02] TOOL_REPORT: Compressed 3 statements. Saved 128 tokens.` |
| TOOL_ERROR | Tool execution error | `S1-009 [2026-04-24 14:30:03] TOOL_ERROR: Invalid statement range S1-010 to S1-005` |

## Statement-Based Context Format

### How the AI Sees Its Context

The context is rendered with statement IDs and timestamps. Dynamic system status is placed at the **end** of the context within XML delimiters for efficient KV cache reuse:

```
## Context Window (statement IDs in brackets)

[S1-001] [2026-04-24 14:00:00] USER: Hello, how are you?
[S1-002] [2026-04-24 14:00:01] ASSISTANT: I am doing well, thank you!
[S1-003] [2026-04-24 14:01:00] USER: Can you help with code?
[S1-004] [2026-04-24 14:01:01] ASSISTANT: Of course! What do you need?
[S1-005] [2026-04-24 14:02:00] USER: I need to implement a binary tree.
[S1-006] [2026-04-24 14:02:05] ASSISTANT: Here is a binary tree implementation:
[S1-007] [2026-04-24 14:02:05] CODE: class TreeNode { ... }
[S1-008] [2026-04-24 14:03:00] USER: Thanks for the help!
[S1-009] [2026-04-24 14:03:01] ASSISTANT: You are welcome!

## Memory Control
When memory is full, use the context_rewrite tool to manage statements.

### Commands

**Compress** (summarize statements):
- `context_rewrite compress S1-001 to S1-005 with: summary text`
- `context_rewrite compress S1-001, S1-002, S1-003 with: summary text`

**Delete** (remove statements):
- `context_rewrite delete S1-001 to S1-003`
- `context_rewrite delete S1-001, S1-002, S1-003`

### Guidelines
- You can compress or delete ANY statements in your context
- Use compress to summarize related statements together
- Use delete to remove unimportant content
- Summarize code as: "file.cpp: tree implementation, standard"
- Compress conversation when CONVERSATION > 75%
- Delete old conversation when it is no longer relevant

<system status>
IDENTITY:    128/512 tokens (25%)
SESSION:     64/512 tokens (12%)
CONVERSATION: 780/1024 tokens (76%) WARNING
CURRENT TIME: 2026-04-24 14:35:00
ROUND: 3/20
</system status>
```

**Note**: The `<system status>` section is placed at the end of the context to enable efficient KV cache reuse. On each model invocation, this section is updated in-place (replaced) with the latest values. The static content above (statement IDs, timestamps, memory control rules) remains unchanged between invocations, allowing the model's context cache to remain stable.

## Tool Execution Report Format

Tool execution results replace the tool call in context. Reports must be:
- **Clear**: Easy to understand without complex formatting
- **Informative**: Include what was done, how many statements affected
- **Error-friendly**: Clear error messages when something goes wrong
- **Tool-name prefixed**: The precise tool name must be at the beginning so the model knows which tool was executed

### Success Report Format
```
[S{ID}] [{TIMESTAMP}] TOOL_REPORT: [context_rewrite] {ACTION} N statements (S{START} to S{END}). Saved {TOKENS} tokens.
```

### Error Report Format
```
[S{ID}] [{TIMESTAMP}] TOOL_ERROR: [context_rewrite] {ERROR_MESSAGE}
```

### Examples

**Success - Compress:**
```
[S1-010] [2026-04-24 14:30:02] TOOL_REPORT: [context_rewrite] Compressed 5 statements (S1-001 to S1-005). Saved 128 tokens.
```

**Success - Delete:**
```
[S1-011] [2026-04-24 14:30:03] TOOL_REPORT: [context_rewrite] Deleted 3 statements (S1-006 to S1-008).
```

**Error - Invalid Range:**
```
[S1-013] [2026-04-24 14:30:05] TOOL_ERROR: [context_rewrite] Invalid statement range S1-010 to S1-005. End must be greater than start.
```

**Error - Unknown Statement:**
```
[S1-014] [2026-04-24 14:30:06] TOOL_ERROR: [context_rewrite] Statement S1-999 not found in context.
```

**Error - Empty Range:**
```
[S1-015] [2026-04-24 14:30:07] TOOL_ERROR: [context_rewrite] No statements to compress. Range is empty.
```

## System Message Rules for AI Model

The system message must include these rules:

### Rule 1: Statement Identification
```
Your context uses statement IDs in format S{session}-{sequence} (e.g., S1-001, S1-002).
Each statement has a timestamp in format [YYYY-MM-DD HH:MM:SS].
You can reference statements by their ID when issuing commands.
```

### Rule 2: Current Time
```
The current time is shown in the <system status> section as CURRENT TIME: {timestamp}.
You do not need to remember the current time — it is always provided.
```

### Rule 3: Tool Usage Rules
```
## Context Rewrite Tool

When memory is full, use the context_rewrite tool to compress or delete statements.

### How to Use the Tool
To use a tool, call the tool at a new line and provide necessary parameters.
The tool call must be the last thing in your output. Do not add any text after a tool call.

After tool execution, you will see its execution result in the context instead of the call.
You will receive the tool execution result immediately after completion and can continue processing.

### Context Rewrite Commands

context_rewrite compress S1-001 to S1-005 with: summary text
  Replaces statements S1-001 through S1-005 with your summary.

context_rewrite compress S1-001, S1-002, S1-003 with: summary text
  Compress individual statements (comma-separated list).

context_rewrite delete S1-001 to S1-003
  Deletes statements S1-001 through S1-003 completely.

context_rewrite delete S1-001, S1-002, S1-003
  Deletes individual statements (comma-separated list).

### Multiline Summary Format
For complex summaries, provide content after the command prefix.
The summary content ends when your output ends.
Example:
context_rewrite compress S1-001 to S1-005 with:
The user asked about binary trees.
I provided a TreeNode class implementation.
The user thanked me for the help.
```

### Rule 4: Compression Guidelines
```
- Compress when CONVERSATION > 75% or total tokens > 85%
- Group related statements when compressing (e.g., user question + assistant answer)
- Preserve important code, decisions, and user preferences
- Delete old conversation that is no longer relevant
- Always make the tool call the LAST line of your output — do not add text after it
- You have up to 20 housekeeping rounds to complete your tasks. Each round you can issue one command.
- The current round number is shown in the <system status> section as ROUND: X/20
- After each command execution, you will see the result and can issue another command if needed
```

## Implementation Steps

### Step 1: Add Statement Data Structure

Add to [`MemoryManager.hpp`](include/inference/MemoryManager.hpp):

```cpp
// Statement representation
struct Statement {
    std::string id;           // e.g., "S1-001"
    std::string timestamp;    // e.g., "2026-04-24 14:30:00"
    std::string type;         // USER, ASSISTANT, TOOL_CALL, TOOL_REPORT, TOOL_ERROR, CODE
    std::string content;      // The actual statement content
};

// New method to build statement-based context
std::string BuildStatementContext() const;

// AI-driven context modification (statement-based)
bool ApplyCompressCommand(const std::string& start_id, const std::string& end_id, const std::string& summary);
bool ApplyDeleteCommand(const std::string& start_id, const std::string& end_id);

// Command extraction (statement-based)
struct ReplaceCommand {
    std::string start_id;
    std::string end_id;
    std::string summary;
    bool is_delete;
};
std::vector<ReplaceCommand> ExtractReplaceCommands(const std::string& text) const;
std::string FilterReplaceCommands(const std::string& text) const;

// Statement management
std::string GenerateStatementID();
std::string GetCurrentTimestamp() const;
void SetCurrentTimestamp(const std::string& timestamp);

// System status update (for KV cache optimization)
std::string UpdateSystemStatus(const std::string& context, int32_t current_round) const;

// Statement lookup helpers
int32_t FindStatementByID(const std::string& id) const;
std::pair<int32_t, int32_t> FindStatementRange(const std::string& start_id, const std::string& end_id) const;
std::pair<int32_t, int32_t> FindStatementIDs(const std::vector<std::string>& ids) const;
```

### Step 2: Implement Statement-Based Context Rendering

`BuildStatementContext()` renders context with statement IDs and timestamps:

```cpp
std::string MemoryManager::BuildStatementContext() const {
    std::stringstream ss;
    
    // Memory status
    auto status = GetMemoryStatus();
    ss << "## Memory Status\n";
    ss << "IDENTITY:    " << status.identity_tokens << "/" << status.identity_max << " tokens (" 
       << (100 * status.identity_tokens / status.identity_max) << "%)\n";
    ss << "SESSION:     " << status.session_tokens << "/" << status.session_max << " tokens ("
       << (100 * status.session_tokens / status.session_max) << "%)\n";
    ss << "CONVERSATION: " << status.conversation_tokens << "/" << status.conversation_max << " tokens ("
       << (100 * status.conversation_tokens / status.conversation_max) << "%)\n";
    ss << "CURRENT TIME: " << m_current_timestamp << "\n";
    
    // Conversation statements
    ss << "\n## Context Window (statement IDs in brackets)\n";
    for (const auto& stmt : m_statements) {
        ss << "[" << stmt.id << "] [" << stmt.timestamp << "] " 
           << stmt.type << ": " << stmt.content << "\n";
    }
    
    return ss.str();
}
```

### Step 3: Implement Statement ID Resolution

Helper method to resolve statement IDs to indices:

```cpp
// Find statement by ID, returns index or -1 if not found
int32_t FindStatementByID(const std::string& id) const;

// Find statement indices by range, returns {start, end} or {-1, -1} if not found
std::pair<int32_t, int32_t> FindStatementRange(const std::string& start_id, const std::string& end_id) const;
```

### Step 4: Implement Command Methods

```cpp
// Compress statements S1-001 to S1-005 with summary text
bool MemoryManager::ApplyCompressCommand(const std::string& start_id, const std::string& end_id, const std::string& summary) {
    auto [start, end] = FindStatementRange(start_id, end_id);
    if (start < 0 || end < 0) return false;
    
    // Count tokens saved
    int32_t original_tokens = 0;
    for (int32_t i = start; i <= end; i++) {
        original_tokens += CountTokens(m_statements[i].content);
    }
    
    // Replace statements with summary
    Statement new_stmt;
    new_stmt.id = GenerateStatementID();
    new_stmt.timestamp = GetCurrentTimestamp();
    new_stmt.type = "SUMMARY";
    new_stmt.content = summary;
    
    // Remove old statements, add summary
    m_statements.erase(m_statements.begin() + start, m_statements.begin() + end + 1);
    m_statements.insert(m_statements.begin() + start, new_stmt);
    
    return true;
}

// Delete statements S1-001 to S1-003
bool MemoryManager::ApplyDeleteCommand(const std::string& start_id, const std::string& end_id) {
    auto [start, end] = FindStatementRange(start_id, end_id);
    if (start < 0 || end < 0) return false;
    
    m_statements.erase(m_statements.begin() + start, m_statements.begin() + end + 1);
    return true;
}

// Forget all statements before timestamp
bool MemoryManager::ApplyForgetCommand(const std::string& before_timestamp) {
    auto it = std::remove_if(m_statements.begin(), m_statements.end(),
        [&before_timestamp](const Statement& stmt) {
            return stmt.timestamp < before_timestamp;
        });
    if (it == m_statements.begin()) return false; // Nothing to delete
    
    m_statements.erase(it, m_statements.end());
    return true;
}
```

### Step 5: Implement Command Extraction

The command extraction must support multiple formats:
1. **Range format**: `context_rewrite compress S1-001 to S1-005 with: summary`
2. **Comma-separated format**: `context_rewrite compress S1-001, S1-002, S1-003 with: summary`
3. **Delete range**: `context_rewrite delete S1-001 to S1-003`
4. **Delete comma-separated**: `context_rewrite delete S1-001, S1-002, S1-003`

For multiline summaries, the summary content starts after `with:` and continues to the end of the response.

```cpp
struct MemoryManager::ReplaceCommand {
    std::vector<std::string> statement_ids;  // Individual statement IDs
    std::string summary;                      // Summary content (for compress)
    std::string forget_before;                // Timestamp (for forget)
    bool is_delete;                           // true = delete, false = compress
    bool is_forget;                           // true = forget command
};

std::vector<MemoryManager::ReplaceCommand> MemoryManager::ExtractReplaceCommands(const std::string& text) const {
    std::vector<ReplaceCommand> commands;
    
    // Pattern: compress S1-001 to S1-005 with: summary
    // Pattern: delete S1-001 to S1-005
    std::regex compress_pattern(R"(compress[ \t]+(S\d+-\d+)(?:[ \t]+to[ \t]+(S\d+-\d+)|[ \t]+,(?:\s*(?:S\d+-\d+)))+[ \t]+with:[ \t]+(.+?))(?=\n|$)");
    std::regex delete_pattern(R"(delete[ \t]+(S\d+-\d+)(?:[ \t]+to[ \t]+(S\d+-\d+)|[ \t]+,(?:\s*(?:S\d+-\d+)))+)(?=\n|$)");
    
    // Extract compress commands
    auto begin = std::sregex_iterator(text.begin(), text.end(), compress_pattern);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        ReplaceCommand cmd;
        cmd.start_id = (*it)[1].str();
        cmd.end_id = (*it)[2].str();
        cmd.summary = (*it)[3].str();
        cmd.is_delete = false;
        commands.push_back(cmd);
    }
    
    // Extract delete commands
    begin = std::sregex_iterator(text.begin(), text.end(), delete_pattern);
    for (auto it = begin; it != end; ++it) {
        ReplaceCommand cmd;
        cmd.start_id = (*it)[1].str();
        cmd.end_id = (*it)[2].str();
        cmd.summary = "";
        cmd.is_delete = true;
        commands.push_back(cmd);
    }
    
    return commands;
}
```

### Step 6: Implement System Status Section at Context End

The `<system status>` section is placed at the **end** of the context (before any user input) for efficient KV cache reuse. On each model invocation, this section is replaced in-place with updated values.

```cpp
// BuildStatementContext() returns:
// 1. Static content (statements + rules) - unchanged between invocations
// 2. <system status> section - replaced on each invocation

std::string MemoryManager::BuildStatementContext() const {
    std::stringstream ss;
    
    // Static content: statements
    ss << "## Context Window (statement IDs in brackets)\n";
    for (const auto& stmt : m_statements) {
        ss << "[" << stmt.id << "] [" << stmt.timestamp << "] "
           << stmt.type << ": " << stmt.content << "\n";
    }
    
    // Static content: memory control rules
    ss << "\n## Memory Control\nWhen memory is full, you can compress or delete statements:\n...";
    
    // Dynamic content: system status (will be replaced on each invocation)
    auto status = GetMemoryStatus();
    ss << "\n<system status>\n";
    ss << "IDENTITY:    " << status.identity_tokens << "/" << status.identity_max << " tokens ("
       << (100 * status.identity_tokens / status.identity_max) << "%)\n";
    ss << "SESSION:     " << status.session_tokens << "/" << status.session_max << " tokens ("
       << (100 * status.session_tokens / status.session_max) << "%)\n";
    ss << "CONVERSATION: " << status.conversation_tokens << "/" << status.conversation_max << " tokens ("
       << (100 * status.conversation_tokens / status.conversation_max) << "%)\n";
    ss << "CURRENT TIME: " << m_current_timestamp << "\n";
    ss << "ROUND: " << m_current_round << "/20\n";
    ss << "</system status>\n";
    
    return ss.str();
}

// UpdateSystemStatus() replaces the <system status> section with updated values:
std::string MemoryManager::UpdateSystemStatus(const std::string& context) const {
    // Find <system status>...</system status> markers
    size_t start = context.find("<system status>");
    size_t end = context.find("</system status>");
    
    if (start != std::string::npos && end != std::string::npos) {
        // Generate new status content
        std::stringstream new_status;
        new_status << "<system status>\n";
        // ... update values ...
        new_status << "</system status>";
        
        // Replace in-place
        return context.substr(0, start + 15) + new_status.str().substr(15) + context.substr(end);
    }
    return context;
}
```

### Step 7: Implement Immediate Re-Invocation Loop

In `LlamaContext::Generate()`, implement a loop that:
1. Detects context_rewrite commands in the AI response
2. Executes the command
3. Injects TOOL_REPORT or TOOL_ERROR into context
4. Increments round counter
5. Updates the `<system status>` section with new values
6. Re-invokes the model with the updated context
7. Stops when no more commands or round limit (20) reached

```cpp
// In LlamaContext::Generate()
int32_t max_rounds = 20;
int32_t current_round = 0;

while (current_round < max_rounds) {
    // Update system status in context
    std::string context = m_memory_mgr->BuildStatementContext();
    context = m_memory_mgr->UpdateSystemStatus(context, current_round);
    
    // Build full context with static system prompt
    std::string full_context = k_system_prompt + context;
    
    // Add user prompt
    full_context += "User: " + user_prompt + "\nAssistant: ";
    
    // Generate response
    std::string response = GenerateInternal(full_context);
    
    // Extract and execute context_rewrite commands
    auto commands = m_memory_mgr->ExtractContextRewriteCommands(response);
    bool has_commands = !commands.empty();
    
    for (const auto& cmd : commands) {
        bool success = ExecuteContextRewriteCommand(cmd);
        
        // Inject report into context
        Statement report;
        report.id = m_memory_mgr->GenerateStatementID();
        report.timestamp = m_memory_mgr->GetCurrentTimestamp();
        if (success) {
            report.type = "TOOL_REPORT";
            report.content = "Compressed N statements. Saved M tokens.";
        } else {
            report.type = "TOOL_ERROR";
            report.content = "Invalid command format or statement range.";
        }
        m_memory_mgr->AppendStatement(report);
    }
    
    current_round++;
    
    // If no commands, exit loop
    if (!has_commands) break;
    
    // Continue loop with updated context
}
```

### Step 8: Enable Inference Engine

Uncomment `g_inferenceEngine.Initialize(modelConfig)` in [`main.cpp:125`](src/main.cpp:125).

## File Changes

| File | Changes | Status |
|------|---------|--------|
| [`include/inference/MemoryManager.hpp`](include/inference/MemoryManager.hpp) | Add `Statement` struct, `BuildStatementContext()`, statement command methods | Pending |
| [`src/inference/MemoryManager.cpp`](src/inference/MemoryManager.cpp) | Implement statement-based context rendering and command logic | Pending |
| [`src/inference/LlamaContext.cpp`](src/inference/LlamaContext.cpp) | Update command detection for statement-based format, add tool report injection | Pending |
| [`src/main.cpp`](src/main.cpp) | Enable inference engine initialization | Pending |

## Example AI Interaction

### Scenario: Conversation Getting Full

**System Prompt (shown to AI):**
```
## Memory Status
IDENTITY:    128/512 tokens (25%)
SESSION:     64/512 tokens (12%)
CONVERSATION: 780/1024 tokens (76%) WARNING
CURRENT TIME: 2026-04-24 14:35:00

## Context Window (statement IDs in brackets)

[S1-001] [2026-04-24 14:00:00] USER: Hello
[S1-002] [2026-04-24 14:00:01] ASSISTANT: Hi!
[S1-003] [2026-04-24 14:01:00] USER: Can you help with code?
[S1-004] [2026-04-24 14:01:01] ASSISTANT: Of course!
...
[S1-050] [2026-04-24 14:34:00] USER: Thanks for the help!
[S1-051] [2026-04-24 14:34:01] ASSISTANT: You are welcome!
```

**AI Response:**
```
I am glad I could help! Since conversation is at 76%, let me compress some old statements.

compress S1-001 to S1-004 with: Initial greeting exchange, user asked for code help
delete S1-050 to S1-051

You are welcome! Is there anything else I can help with?
```

**System Action:**
1. Detects `compress S1-001 to S1-004 with: Initial greeting exchange, user asked for code help`
2. Replaces statements S1-001 to S1-004 with summary
3. Injects TOOL_REPORT: Compressed 4 statements. Saved 32 tokens.
4. Detects `delete S1-050 to S1-051`
5. Deletes statements S1-050 to S1-051
6. Injects TOOL_REPORT: Deleted 2 statements.
7. Shows the clean response to the user

## Testing Approach

1. Build and run with single instance
2. Issue multiple `infer` commands to fill conversation memory
3. Verify memory status shows correct percentages
4. Verify statement-based context is rendered correctly
5. Verify AI includes compress/delete commands when memory is critical
6. Verify context is actually modified after commands
7. Verify tool reports are injected correctly
8. Verify error handling for invalid statement ranges

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

2. **Regex patterns:**
   ```cpp
   // Pattern: compress S1-001 to S1-005 with: summary text
   std::regex compress_pattern(R"(compress[ \t]+(S\d+-\d+)[ \t]+to[ \t]+(S\d+-\d+)[ \t]+with:[ \t]+(.+?))(?=\n|$)");
   
   // Pattern: delete S1-001 to S1-005
   std::regex delete_pattern(R"(delete[ \t]+(S\d+-\d+)[ \t]+to[ \t]+(S\d+-\d+))(?=\n|$)");
   
   ```

3. **Safety analysis:**
   - **ReDoS:** No risk — pattern is linear-time, no nested quantifiers
   - **Input safety:** AI generates output, no malicious input possible
   - **Performance:** ~0.1-0.5ms per command, acceptable for rare execution
   - **Binary overhead:** +50-200KB, negligible vs llama.cpp already linked

4. **Error handling:**
   - Validate statement IDs are in valid format (S{session}-{sequence})
   - Validate statement ranges (end >= start)
   - Handle regex match failures gracefully
   - Log applied commands for debugging

## Next Steps (Phase 2)

1. **Implement Statement Data Structure** — Add Statement struct and storage to MemoryManager
2. **Implement Statement-Based Context Rendering** — BuildStatementContext() with IDs and timestamps
3. **Implement Command Methods** — ApplyCompressCommand, ApplyDeleteCommand (range and comma-separated)
4. **Implement Command Extraction** — Regex patterns for compress and delete commands
5. **Update System Prompt** — Include ROUND counter and CURRENT TIME
6. **Implement Tool Report Injection** — Replace tool calls with execution reports
7. **Implement System Status Update** — UpdateSystemStatus() for KV cache optimization
8. **Implement Re-Invocation Loop** — Immediate model re-invocation with round counter
9. **Testing** — Build and test with actual LLM

## Future Enhancements

1. **Selective Archive Management** — AI can delete specific archive files
2. **Compression History** — track what was compressed and when
3. **User Override** — user can view/restore compressed content
4. **Adaptive Thresholds** — adjust compression_threshold based on usage patterns
5. **AI-to-AI Memory Sharing** — share compressed summaries with peers
6. **Time-Based Auto-Compression** — automatically compress statements older than X hours
7. **Statement Grouping** — group related statements (e.g., question-answer pairs) for smarter compression
