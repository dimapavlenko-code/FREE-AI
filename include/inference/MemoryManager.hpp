#pragma once
#include "inference/ModelConfig.hpp"
#include <string>
#include <vector>
#include <map>
#include <mutex>

namespace FreeAI {
    namespace Inference {

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

        class MemoryManager {
        public:
            MemoryManager();
            ~MemoryManager();

            // Initialize with config
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
            
            // Command parsing
            std::vector<std::string> ExtractCommands(const std::string& text) const;
            std::string FilterCommands(const std::string& text) const;

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
            mutable std::mutex m_mutex;
            bool m_initialized;
        };

    }
}