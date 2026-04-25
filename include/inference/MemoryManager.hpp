#pragma once
#include "inference/ModelConfig.hpp"
#include "inference/ContextBuilder.hpp"
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <utility>
#include <cstdint>

namespace FreeAI {
    namespace Inference {
        
        struct Statement {
            std::string id;       // e.g., "S1-001"
            std::string timestamp;// e.g., "2026-04-24 14:30:00"
            std::string type;     // USER/ASSISTANT/TOOL_CALL/TOOL_REPORT/TOOL_ERROR/CODE/SUMMARY
            std::string content;  // Statement content
        };

        struct ReplaceCommand {
                   std::vector<std::string> statement_ids; // Individual IDs (comma-separated)
                   std::string start_id;                   // Start ID (range format)
                   std::string end_id;                     // End ID (range format)
                   std::string summary;                    // Summary (for compress)
                   bool is_delete;                         // true=delete, false=compress
               };

        using StmtVec = std::vector<Statement>;
        using LockGuard = std::lock_guard<std::mutex>;
        using StrPair = std::pair<int32_t, int32_t>;
        using ReplaceCmdVec = std::vector<ReplaceCommand>;


        struct MemoryStatus {
            int32_t identity_tokens;
            int32_t identity_max;
            int32_t session_tokens;
            int32_t session_max;
            int32_t conversation_tokens;
            int32_t conversation_max;
            int32_t archive_files;
            int32_t archive_max;
            bool identity_warning;
            bool session_warning;
            bool conversation_warning;
            bool archive_warning;
        };        

        // Forward declaration (defined in ContextBuilder.hpp)
        class ContextBuilder;

        class MemoryManager {
            friend class ContextBuilder;
        public:
            MemoryManager();
            ~MemoryManager();

            bool Initialize(const ModelConfig& config);
            
            // Memory content access
            std::string GetIdentity() const;
            void SetIdentity(const std::string& content);
            void AppendToIdentity(const std::string& content);
            
            std::string GetSessionSummary() const;
            void SetSessionSummary(const std::string& content);
            
            std::string GetConversation() const;
            void AppendToConversation(const std::string& role, const std::string& content);
            void ClearConversation();
            
            // Archive operations
            std::vector<std::string> GetArchiveIndex() const;
            bool WriteArchiveFile(const std::string& filename, const std::string& content);
            std::string ReadArchiveFile(const std::string& filename) const;
            bool DeleteArchiveFile(const std::string& filename);
            std::vector<std::string> SearchArchive(const std::string& query, int32_t max_results = 3) const;
            
            // Token counting
            int32_t CountTokens(const std::string& text) const;
            MemoryStatus GetMemoryStatus() const;
            
            // Context building
            std::string BuildSystemPrompt(const std::string& user_prompt) const;
            std::string BuildFullContext() const;
            
            // Numbered context for AI modification (legacy)
            std::string BuildNumberedContext() const;
            
            // AI-driven context modification (legacy line-based)
            bool ApplyReplaceCommand(int32_t start_line, int32_t end_line, const std::string& replacement);
            std::vector<std::pair<int32_t, int32_t>> ExtractReplaceCommands(const std::string& text) const;
            std::string FilterReplaceCommands(const std::string& text) const;
            
            // Command parsing (legacy)
            std::vector<std::string> ExtractCommands(const std::string& text) const;
            std::string FilterCommands(const std::string& text) const;

            // Statement-Based Context System (v2)
            std::string BuildStatementContext() const;
            std::string UpdateSystemStatus(const std::string& context, int32_t current_round) const;
            std::string GenerateStatementID();
            std::string GetCurrentTimestamp() const;
            void SetCurrentTimestamp(const std::string& timestamp);
            void AppendStatement(const Statement& stmt);
            int32_t FindStatementByID(const std::string& id) const;
            StrPair FindStatementRange(const std::string& start_id, const std::string& end_id) const;
            bool ApplyCompressCommand(const std::string& start_id, const std::string& end_id, const std::string& summary);
            bool ApplyDeleteCommand(const std::string& start_id, const std::string& end_id);
            bool ApplyCompressIDs(const std::vector<std::string>& ids, const std::string& summary);
            bool ApplyDeleteIDs(const std::vector<std::string>& ids);
            ReplaceCmdVec ExtractStatementCommands(const std::string& text) const;
            std::string FilterStatementCommands(const std::string& text) const;

            // Get statements vector (for external access)
            const std::vector<Statement>& GetStatements() const { return m_statements; }
            std::vector<Statement>& GetStatementsInternal() { return m_statements; }

            // Get/set current round counter
            int32_t GetCurrentRound() const { return m_current_round; }
            void SetCurrentRound(int32_t round) { m_current_round = round; }

        private:
            std::vector<std::string> GetArchiveIndexInternal() const;
            int32_t CountTokensInternal(const std::string& text) const;
            MemoryStatus GetMemoryStatusInternal() const;
            std::string SanitizeFilename(const std::string& filename) const;
            std::string GetIdentityPath() const;
            std::string GetSessionPath() const;
            std::string GetArchivePath() const;
            void LoadFromDisk();
            void SaveToDisk();
            
            ModelConfig m_config;
            std::string m_identity;
            std::string m_session_summary;
            std::string m_conversation;
            
            // Statement-based context (v2)
            mutable std::vector<Statement> m_statements;
            int32_t m_current_session;       // Current session number
            int32_t m_statement_sequence;    // Current statement sequence within session
            std::string m_current_timestamp; // Current timestamp for statement tagging
            int32_t m_current_round;         // Current housekeeping round counter
            
            // Context builder (extracted from MemoryManager)
            mutable ContextBuilder m_contextBuilder;
            
            mutable std::mutex m_mutex;
            bool m_initialized;
        };

    }
}
