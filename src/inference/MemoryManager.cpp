#include "inference/MemoryManager.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <regex>
#include <ctime>

namespace fs = std::filesystem;

namespace FreeAI {
	namespace Inference {

		MemoryManager::MemoryManager()
			: m_initialized(false) {
		}

		MemoryManager::~MemoryManager() {
			SaveToDisk();
		}

		bool MemoryManager::Initialize(const ModelConfig& config) {
			std::lock_guard<std::mutex> lock(m_mutex);

			m_config = config;
			m_initialized = true;

			// Create directories
			fs::create_directories("models");
			fs::create_directories(m_config.archive_path);

			// Load existing memory from disk
			LoadFromDisk();

			std::cout << "[MEMORY] Memory manager initialized" << std::endl;
			std::cout << "[MEMORY] Identity: " << CountTokensInternal(m_identity) << " tokens" << std::endl;
			std::cout << "[MEMORY] Archive: " << GetArchiveIndexInternal().size() << "/"
				<< m_config.max_archive_files << " files" << std::endl;  // ← Use internal version

			return true;
		}


		std::string MemoryManager::GetIdentity() const {
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_identity;
		}

		void MemoryManager::SetIdentity(const std::string& content) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_identity = content;
			SaveToDisk();
		}

		void MemoryManager::AppendToIdentity(const std::string& content) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_identity += "\n" + content;
			SaveToDisk();
		}

		std::string MemoryManager::GetSessionSummary() const {
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_session_summary;
		}

		void MemoryManager::SetSessionSummary(const std::string& content) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_session_summary = content;
			SaveToDisk();
		}

		std::string MemoryManager::GetConversation() const {
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_conversation;
		}

		void MemoryManager::AppendToConversation(const std::string& role, const std::string& content) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_conversation += role + ": " + content + "\n";
		}

		void MemoryManager::ClearConversation() {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_conversation.clear();
		}

		// Internal version(no locking)
		std::vector<std::string> MemoryManager::GetArchiveIndexInternal() const {
			std::vector<std::string> files;

			std::string archive_dir = m_config.archive_path;
			if (!fs::exists(archive_dir)) {
				return files;
			}

			for (const auto& entry : fs::directory_iterator(archive_dir)) {
				if (entry.is_regular_file() && entry.path().extension() == ".txt") {
					files.push_back(entry.path().filename().string());
				}
			}

			std::sort(files.begin(), files.end());
			return files;
		}

		// Public version (with locking)
		std::vector<std::string> MemoryManager::GetArchiveIndex() const {
			std::lock_guard<std::mutex> lock(m_mutex);
			return GetArchiveIndexInternal();
		}


		bool MemoryManager::WriteArchiveFile(const std::string& filename, const std::string& content) {
			std::lock_guard<std::mutex> lock(m_mutex);

			std::string safe_filename = SanitizeFilename(filename);
			std::string filepath = m_config.archive_path + "/" + safe_filename;

			// Check file limit
			auto current_files = GetArchiveIndex();
			if (current_files.size() >= static_cast<size_t>(m_config.max_archive_files)) {
				std::cerr << "[MEMORY] Archive full (" << m_config.max_archive_files << " files)" << std::endl;
				return false;
			}

			std::ofstream file(filepath);
			if (!file.is_open()) {
				std::cerr << "[MEMORY] Failed to write archive file: " << safe_filename << std::endl;
				return false;
			}

			file << content;
			file.close();

			std::cout << "[MEMORY] Wrote archive file: " << safe_filename << std::endl;
			return true;
		}

		std::string MemoryManager::ReadArchiveFile(const std::string& filename) const {
			std::lock_guard<std::mutex> lock(m_mutex);

			std::string safe_filename = SanitizeFilename(filename);
			std::string filepath = m_config.archive_path + "/" + safe_filename;

			if (!fs::exists(filepath)) {
				return "";
			}

			std::ifstream file(filepath);
			if (!file.is_open()) {
				return "";
			}

			std::stringstream buffer;
			buffer << file.rdbuf();
			return buffer.str();
		}

		bool MemoryManager::DeleteArchiveFile(const std::string& filename) {
			std::lock_guard<std::mutex> lock(m_mutex);

			std::string safe_filename = SanitizeFilename(filename);
			std::string filepath = m_config.archive_path + "/" + safe_filename;

			if (!fs::exists(filepath)) {
				return false;
			}

			fs::remove(filepath);
			std::cout << "[MEMORY] Deleted archive file: " << safe_filename << std::endl;
			return true;
		}

		std::vector<std::string> MemoryManager::SearchArchive(const std::string& query, int32_t max_results) const {
			std::lock_guard<std::mutex> lock(m_mutex);

			std::vector<std::pair<std::string, int>> results;  // filename, match_count
			auto files = GetArchiveIndex();

			for (const auto& filename : files) {
				std::string content = ReadArchiveFile(filename);
				int match_count = 0;

				// Simple keyword search
				std::string lower_content = content;
				std::string lower_query = query;
				std::transform(lower_content.begin(), lower_content.end(), lower_content.begin(), ::tolower);
				std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

				// Count keyword occurrences
				size_t pos = 0;
				while ((pos = lower_content.find(lower_query, pos)) != std::string::npos) {
					match_count++;
					pos++;
				}

				// Also check filename
				std::string lower_filename = filename;
				std::transform(lower_filename.begin(), lower_filename.end(), lower_filename.begin(), ::tolower);
				if (lower_filename.find(lower_query) != std::string::npos) {
					match_count += 5;  // Bonus for filename match
				}

				if (match_count > 0) {
					results.push_back({ filename, match_count });
				}
			}

			// Sort by match count
			std::sort(results.begin(), results.end(),
				[](const auto& a, const auto& b) { return a.second > b.second; });

			// Return top results
			std::vector<std::string> top_files;
			for (size_t i = 0; i < static_cast<size_t>(max_results) && i < results.size(); ++i) {
				top_files.push_back(results[i].first);
			}

			return top_files;
		}

		// Update CountTokens to have internal version
		int32_t MemoryManager::CountTokensInternal(const std::string& text) const {
			// Simple approximation: 4 chars ≈ 1 token
			return static_cast<int32_t>(text.size() / 4);
		}

		int32_t MemoryManager::CountTokens(const std::string& text) const {
			std::lock_guard<std::mutex> lock(m_mutex);
			return CountTokensInternal(text);
		}


		MemoryStatus MemoryManager::GetMemoryStatusInternal() const {
			MemoryStatus status;

			int32_t identity_max = (m_config.n_ctx * m_config.identity_ratio) / 100;
			int32_t session_max = (m_config.n_ctx * m_config.session_ratio) / 100;
			int32_t conversation_max = (m_config.n_ctx * m_config.conversation_ratio) / 100;

			status.identity_tokens = CountTokensInternal(m_identity);
			status.identity_max = identity_max;
			status.session_tokens = CountTokensInternal(m_session_summary);
			status.session_max = session_max;
			status.conversation_tokens = CountTokensInternal(m_conversation);
			status.conversation_max = conversation_max;
			status.archive_files = static_cast<int32_t>(GetArchiveIndexInternal().size());
			status.archive_max = m_config.max_archive_files;

			status.identity_warning = status.identity_tokens > (identity_max * m_config.compression_threshold / 100);
			status.session_warning = status.session_tokens > (session_max * m_config.compression_threshold / 100);
			status.conversation_warning = status.conversation_tokens > (conversation_max * m_config.compression_threshold / 100);
			status.archive_warning = status.archive_files > (m_config.max_archive_files * m_config.archive_warning_threshold / 100);

			return status;
		}

		MemoryStatus MemoryManager::GetMemoryStatus() const {
			std::lock_guard<std::mutex> lock(m_mutex);
			return GetMemoryStatusInternal();
		}


		std::string MemoryManager::BuildSystemPrompt(const std::string& user_prompt) const {
			std::lock_guard<std::mutex> lock(m_mutex);

			auto status = GetMemoryStatusInternal();  // ← Use internal version
			auto archive_files = GetArchiveIndexInternal();  // ← Use internal version

			// Get current time
			auto now = std::time(nullptr);
			auto tm = *std::localtime(&now);
			char time_buf[64];
			std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S UTC", &tm);

			std::stringstream ss;

			ss << "<|im_start|>system\n";
			ss << "You are Qwen, an AI assistant with persistent memory.\n\n";

			ss << "## Current Time\n";
			ss << "Date: " << time_buf << "\n\n";

			ss << "## Your Memory Structure\n";
			ss << "- IDENTITY: " << status.identity_tokens << "/" << status.identity_max
				<< " tokens (" << (status.identity_tokens * 100 / std::max(1, status.identity_max)) << "%)";
			if (status.identity_warning) ss << " ⚠️ Recommended: Compress at " << m_config.compression_threshold << "%";
			ss << "\n";

			ss << "- SESSION: " << status.session_tokens << "/" << status.session_max
				<< " tokens (" << (status.session_tokens * 100 / std::max(1, status.session_max)) << "%)";
			if (status.session_warning) ss << " ⚠️ Recommended: Compress at " << m_config.compression_threshold << "%";
			ss << "\n";

			ss << "- CONVERSATION: " << status.conversation_tokens << "/" << status.conversation_max
				<< " tokens (" << (status.conversation_tokens * 100 / std::max(1, status.conversation_max)) << "%)";
			if (status.conversation_warning) ss << " ⚠️ Recommended: Compress at " << m_config.compression_threshold << "%";
			ss << "\n";

			ss << "- ARCHIVE: " << status.archive_files << "/" << status.archive_max << " files";
			if (status.archive_warning) ss << " ⚠️ Recommended: Delete old files at " << m_config.archive_warning_threshold << "%";
			ss << "\n\n";

			ss << "## Your Archive Files (Complete Index)\n";
			if (archive_files.empty()) {
				ss << "(No files yet)\n";
			}
			else {
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

			ss << "## Recommendations\n";
			ss << "- Store important user preferences with /store\n";
			ss << "- Compress conversation when CONVERSATION > " << m_config.compression_threshold << "% full\n";
			ss << "- Recall relevant memories before answering complex questions\n";
			ss << "- Archive files limited to " << m_config.max_archive_files << " - you decide which to keep\n";
			ss << "- You are not forced to use all files - store self-recommendations in identity\n\n";

			ss << "## Current User Request\n";
			ss << user_prompt << "\n";
			ss << "<|im_end|>\n";

			return ss.str();
		}

		std::string MemoryManager::BuildFullContext() const {
			std::lock_guard<std::mutex> lock(m_mutex);

			std::stringstream ss;

			// Identity (always included)
			if (!m_identity.empty()) {
				ss << "## Identity Memory\n";
				ss << m_identity << "\n\n";
			}

			// Session summary (always included)
			if (!m_session_summary.empty()) {
				ss << "## Session Summary\n";
				ss << m_session_summary << "\n\n";
			}

			// Recent conversation (always included)
			if (!m_conversation.empty()) {
				ss << "## Recent Conversation\n";
				ss << m_conversation << "\n\n";
			}

			return ss.str();
		}

		std::vector<std::string> MemoryManager::ExtractCommands(const std::string& text) const {
			std::vector<std::string> commands;

			// Match /command arg patterns
			std::regex command_pattern(R"(/(\w+)(?:\s+(.*))?)");
			auto begin = std::sregex_iterator(text.begin(), text.end(), command_pattern);
			auto end = std::sregex_iterator();

			for (auto it = begin; it != end; ++it) {
				std::string cmd = "/" + it->str(1);
				std::string arg = it->str(2);
				if (!arg.empty()) {
					cmd += " " + arg;
				}
				commands.push_back(cmd);
			}

			return commands;
		}

		std::string MemoryManager::FilterCommands(const std::string& text) const {
			std::regex command_pattern(R"(/\w+(?:\s+[^/\n]*)?)");
			return std::regex_replace(text, command_pattern, "");
		}

		std::string MemoryManager::SanitizeFilename(const std::string& filename) const {
			std::string safe = filename;

			// Replace prohibited characters
			const std::string prohibited = "<>:\"/\\|?*";
			for (char c : prohibited) {
				std::replace(safe.begin(), safe.end(), c, '_');
			}

			// Remove control characters
			safe.erase(std::remove_if(safe.begin(), safe.end(),
				[](char c) { return c < 32; }), safe.end());

			// Limit length
			if (safe.length() > 100) {
				safe = safe.substr(0, 100);
			}

			// Ensure .txt extension
			if (safe.length() < 4 || safe.substr(safe.length() - 4) != ".txt") {
				safe += ".txt";
			}

			return safe;
		}

		std::string MemoryManager::GetIdentityPath() const {
			return "models/identity.txt";
		}

		std::string MemoryManager::GetSessionPath() const {
			return "models/session_summary.txt";
		}

		std::string MemoryManager::GetArchivePath() const {
			return m_config.archive_path;
		}

		void MemoryManager::LoadFromDisk() {
			// Load identity
			std::string identity_path = GetIdentityPath();
			if (fs::exists(identity_path)) {
				std::ifstream file(identity_path);
				if (file.is_open()) {
					std::stringstream buffer;
					buffer << file.rdbuf();
					m_identity = buffer.str();
				}
			}

			// Load session summary
			std::string session_path = GetSessionPath();
			if (fs::exists(session_path)) {
				std::ifstream file(session_path);
				if (file.is_open()) {
					std::stringstream buffer;
					buffer << file.rdbuf();
					m_session_summary = buffer.str();
				}
			}

			// Conversation is NOT loaded (starts fresh each session)
			m_conversation.clear();
		}

		void MemoryManager::SaveToDisk() {
			if (!m_initialized) return;

			// Save identity
			std::string identity_path = GetIdentityPath();
			std::ofstream identity_file(identity_path);
			if (identity_file.is_open()) {
				identity_file << m_identity;
				identity_file.close();
			}

			// Save session summary
			std::string session_path = GetSessionPath();
			std::ofstream session_file(session_path);
			if (session_file.is_open()) {
				session_file << m_session_summary;
				session_file.close();
			}
		}

	}
}