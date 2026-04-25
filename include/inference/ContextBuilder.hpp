#pragma once

#include <string>
#include <vector>

namespace FreeAI {
    namespace Inference {

        class MemoryManager;
        struct MemoryStatus;

        // =====================================================
        // Context Builder - Handles all context building logic
        // Extracted from MemoryManager to reduce code size
        // =====================================================

        class ContextBuilder {
        public:
            // Constructor takes const pointer to MemoryManager
            explicit ContextBuilder(const MemoryManager* manager);

            // Build system prompt with memory status and context
            // Combines identity, session, conversation with memory warnings
            std::string BuildSystemPrompt(const std::string& user_prompt) const;

            // Build statement-based context with system status
            // Used for statement-based context system (v2)
            std::string BuildStatementContext() const;

            // Update system status markers in context string
            // Replaces <system status>...</system status> block with updated values
            std::string UpdateSystemStatus(const std::string& context, int32_t current_round) const;

            // Build numbered context for AI modification (legacy)
            // Adds line numbers to identity, session, conversation for AI to reference
            std::string BuildNumberedContext() const;

            // Build system status XML block
            static std::string BuildStatusBlock(
                const MemoryStatus& status,
                const std::string& current_time,
                int32_t current_round
            );

            // Get current timestamp string
            static std::string GetCurrentTimestamp();

        private:
            const MemoryManager* m_manager;

            // Helper: format memory status percentages
            static std::string FormatPercentage(int32_t tokens, int32_t max_tokens);

            // Helper: get archive file list
            std::vector<std::string> GetArchiveFiles() const;
        };

    }
}
