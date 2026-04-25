#include "inference/ContextBuilder.hpp"
#include "inference/MemoryManager.hpp"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace FreeAI {
namespace Inference {

// ======================================================
// ContextBuilder Implementation
// ======================================================

// Constructor
ContextBuilder::ContextBuilder(const MemoryManager* manager)
    : m_manager(manager) {}

// Helper: format memory status percentages
std::string ContextBuilder::FormatPercentage(int32_t tokens, int32_t max_tokens)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d%%", max_tokens > 0 ? (100 * tokens / max_tokens) : 0);
    return std::string(buf);
}

// Helper: get archive file list
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

// Static: Build system status XML block
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

// Static: Get current timestamp string
std::string ContextBuilder::GetCurrentTimestamp()
{
    auto now = std::time(nullptr);
    struct tm tm_buf;
    localtime_s(&tm_buf, &now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buf);
}

// Build numbered context for AI modification (legacy)
std::string ContextBuilder::BuildNumberedContext() const
{
    std::stringstream ss;
    int line_num = 1;

    // Helper lambda to add lines with numbers
    auto addLines = [&](const std::string& content) {
        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line)) {
            ss << "[" << std::setw(3) << line_num++ << "]  " << line << "\n";
        }
    };

    // Identity
    ss << "## Identity Memory\n";
    line_num++;
    if (!m_manager->m_identity.empty()) {
        addLines(m_manager->m_identity);
    } else {
        ss << "[" << std::setw(3) << line_num++ << "]  (empty)\n";
    }
    ss << "\n";

    // Session Summary
    ss << "## Session Summary\n";
    line_num++;
    if (!m_manager->m_session_summary.empty()) {
        addLines(m_manager->m_session_summary);
    } else {
        ss << "[" << std::setw(3) << line_num++ << "]  (empty)\n";
    }
    ss << "\n";

    // Conversation
    ss << "## Recent Conversation\n";
    line_num++;
    if (!m_manager->m_conversation.empty()) {
        addLines(m_manager->m_conversation);
    } else {
        ss << "[" << std::setw(3) << line_num++ << "]  (empty)\n";
    }

    return ss.str();
}

// Build system prompt with memory status and context
std::string ContextBuilder::BuildSystemPrompt(const std::string& user_prompt) const
{
    auto status = m_manager->GetMemoryStatusInternal();
    auto archive_files = m_manager->GetArchiveIndexInternal();

    // Get current time
    auto now = std::time(nullptr);
    struct tm tm_buf;
    localtime_s(&tm_buf, &now);
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S UTC", &tm_buf);

    std::stringstream ss;

    ss << "</s><system>\n";
    ss << "You are Qwen, an AI assistant with persistent memory.\n\n";

    ss << "## Current Time\n";
    ss << "Date: " << time_buf << "\n\n";

    ss << "## Your Memory Structure\n";
    ss << "- IDENTITY: " << status.identity_tokens << "/" << status.identity_max
       << " tokens (" << (status.identity_tokens * 100 / std::max(1, status.identity_max)) << "%)";
    if (status.identity_warning) ss << " [!] Recommended: Compress at " << m_manager->m_config.compression_threshold << "%";
    ss << "\n";

    ss << "- SESSION: " << status.session_tokens << "/" << status.session_max
       << " tokens (" << (status.session_tokens * 100 / std::max(1, status.session_max)) << "%)";
    if (status.session_warning) ss << " [!] Recommended: Compress at " << m_manager->m_config.compression_threshold << "%";
    ss << "\n";

    ss << "- CONVERSATION: " << status.conversation_tokens << "/" << status.conversation_max
       << " tokens (" << (status.conversation_tokens * 100 / std::max(1, status.conversation_max)) << "%)";
    if (status.conversation_warning) ss << " [!] Recommended: Compress at " << m_manager->m_config.compression_threshold << "%";
    ss << "\n";

    ss << "- ARCHIVE: " << status.archive_files << "/" << status.archive_max << " files";
    if (status.archive_warning) ss << " [!] Recommended: Delete old files at " << m_manager->m_config.archive_warning_threshold << "%";
    ss << "\n\n";

    ss << "## Your Archive Files (Complete Index)\n";
    if (archive_files.empty()) {
        ss << "(No files yet)\n";
    } else {
        for (const auto& file : archive_files) {
            ss << file << "\n";
        }
    }
    ss << "\n";

    ss << "## Your Capabilities\n";
    ss << "- /store <text> - Save to identity memory (persistent)\n";
    ss << "- /recall <query> - Search archive, inject results into NEXT turn's context\n";
    ss << "- /write <filename> <content> - Write to archive file\n";
    ss << "- /read <filename> - Read archive file into NEXT turn's context\n";
    ss << "- /delete <filename> - Delete archive file\n";
    ss << "- /forget <text> - Remove from identity memory\n";
    ss << "- /summarize - Compress old conversation to session summary\n\n";

    ss << "## Memory Control (AI Autonomy)\n";
    ss << "You have full control over your memory. You can modify your context directly:\n\n";
    ss << "### Commands\n";
    ss << "- replace X-Y with: summary_text\n";
    ss << "  Replace lines X through Y with a summary\n\n";
    ss << "- replace X-Y with: [DELETE]\n";
    ss << "  Delete lines X through Y (use [DELETE] to remove content)\n\n";
    ss << "### Guidelines\n";
    ss << "- You can replace ANY lines in your numbered context\n";
    ss << "- Use [DELETE] to remove unimportant content\n";
    ss << "- Summarize source code as: \"file.cpp: tree implementation, standard\"\n";
    ss << "- Compress conversation when CONVERSATION > " << m_manager->m_config.compression_threshold << "%\n";
    ss << "- Delete old conversation when it's no longer relevant\n";
    ss << "- You decide WHEN to compress based on memory pressure\n\n";

    ss << "## Recommendations\n";
    ss << "- Store important user preferences with /store\n";
    ss << "- Recall relevant memories before answering complex questions\n";
    ss << "- Archive files limited to " << m_manager->m_config.max_archive_files << " - you decide which to keep\n";
    ss << "- You are not forced to use all files - store self-recommendations in identity\n\n";

    // Add numbered context when memory is getting full
    bool any_warning = status.identity_warning || status.session_warning || status.conversation_warning;
    if (any_warning) {
        ss << "## Your Context (with line numbers for modification)\n";
        ss << "You can reference and replace any line using: replace X-Y with: text\n\n";
        ss << BuildNumberedContext() << "\n";
    }

    ss << "## Current User Request\n";
    ss << user_prompt << "\n";
    ss << "</s>\n";

    return ss.str();
}

// Build statement-based context with system status
std::string ContextBuilder::BuildStatementContext() const
{
    std::stringstream ss;
    auto status = m_manager->GetMemoryStatusInternal();

    // Context header
    ss << "## Context Window (statement IDs in brackets)\n\n";

    // Conversation statements
    for (const auto& stmt : m_manager->m_statements) {
        ss << "[" << stmt.id << "] [" << stmt.timestamp << "] "
           << stmt.type << ": " << stmt.content << "\n";
    }

    // Memory control rules
    ss << "\n## Memory Control\n";
    ss << "When memory is full, use the context_rewrite tool to manage statements.\n\n";

    ss << "### Commands\n\n";
    ss << "**Compress** (summarize statements):\n";
    ss << "- `context_rewrite compress S1-001 to S1-005 with: summary text`\n";
    ss << "- `context_rewrite compress S1-001, S1-002, S1-003 with: summary text`\n\n";
    ss << "**Delete** (remove statements):\n";
    ss << "- `context_rewrite delete S1-001 to S1-003`\n";
    ss << "- `context_rewrite delete S1-001, S1-002, S1-003`\n\n";
    ss << "### Guidelines\n";
    ss << "- You can compress or delete ANY statements in your context\n";
    ss << "- Use compress to summarize related statements together\n";
    ss << "- Use delete to remove unimportant content\n";
    ss << "- Summarize code as: \"file.cpp: tree implementation, standard\"\n";

    // Calculate conversation percentage for warning
    int conv_pct = status.conversation_max > 0 ? (100 * status.conversation_tokens / status.conversation_max) : 0;
    if (conv_pct > 75) {
        ss << "- Compress conversation when CONVERSATION > 75% [!]\n";
    } else {
        ss << "- Compress conversation when CONVERSATION > 75%\n";
    }
    ss << "- Delete old conversation when it is no longer relevant\n";

    // System status section (dynamic, will be updated by UpdateSystemStatus)
    ss << "\n" << BuildStatusBlock(status, m_manager->m_current_timestamp, m_manager->m_current_round) << "\n";

    return ss.str();
}

// Update system status markers in context string
std::string ContextBuilder::UpdateSystemStatus(const std::string& context, int32_t current_round) const
{
    auto status = m_manager->GetMemoryStatusInternal();

    // Find <system status>...</system status> markers
    size_t start_marker = context.find("<system status>");
    size_t end_marker = context.find("</system status>");

    if (start_marker == std::string::npos || end_marker == std::string::npos) {
        return context;
    }

    // Generate new status content
    std::stringstream new_status;
    new_status << "<system status>\n";
    new_status << "IDENTITY:    " << status.identity_tokens << "/" << status.identity_max << " tokens ("
               << FormatPercentage(status.identity_tokens, status.identity_max) << ")\n";
    new_status << "SESSION:     " << status.session_tokens << "/" << status.session_max << " tokens ("
               << FormatPercentage(status.session_tokens, status.session_max) << ")\n";
    new_status << "CONVERSATION: " << status.conversation_tokens << "/" << status.conversation_max << " tokens ("
               << FormatPercentage(status.conversation_tokens, status.conversation_max) << ")\n";
    new_status << "CURRENT TIME: " << m_manager->m_current_timestamp << "\n";
    new_status << "ROUND: " << current_round << "/20\n";
    new_status << "</system status>";

    std::string new_status_str = new_status.str();

    // Replace in-place: keep the opening tag, replace content, keep closing tag
    size_t after_start = start_marker + strlen("<system status>");
    size_t before_end = end_marker; // points to '<' of </system status>

    return context.substr(0, after_start) + "\n" + new_status_str.substr(strlen("<system status>")) +
           context.substr(before_end);
}

} // namespace Inference
} // namespace FreeAI
