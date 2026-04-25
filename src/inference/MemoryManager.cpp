#include "inference/MemoryManager.hpp"
#include "inference/ContextBuilder.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <regex>
#include <ctime>
#include <iomanip>

namespace fs = std::filesystem;

namespace FreeAI {
	namespace Inference {

		MemoryManager::MemoryManager()
			: m_initialized(false)
			, m_current_session(1)
			, m_statement_sequence(0)
			, m_current_round(0)
			, m_contextBuilder(this) {}

		MemoryManager::~MemoryManager() { SaveToDisk(); }

		bool MemoryManager::Initialize(const ModelConfig& config) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_config = config;
			m_initialized = true;
			m_current_session = 1;
			m_statement_sequence = 0;
			m_current_round = 0;
			m_current_timestamp = GetCurrentTimestamp();
			fs::create_directories("models");
			fs::create_directories(m_config.archive_path);
			LoadFromDisk();
			std::cout << "[MEMORY] Init done" << std::endl;
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

		// Internal: no locking
		std::vector<std::string> MemoryManager::GetArchiveIndexInternal() const {
			std::vector<std::string> files;
			std::string archive_dir = m_config.archive_path;
			if (!fs::exists(archive_dir)) return files;
			for (const auto& entry : fs::directory_iterator(archive_dir)) {
				if (entry.is_regular_file() && entry.path().extension() == ".txt") {
					files.push_back(entry.path().filename().string());
				}
			}
			std::sort(files.begin(), files.end());
			return files;
		}

		// Public: with locking
		std::vector<std::string> MemoryManager::GetArchiveIndex() const {
			std::lock_guard<std::mutex> lock(m_mutex);
			return GetArchiveIndexInternal();
		}

		bool MemoryManager::WriteArchiveFile(const std::string& filename, const std::string& content) {
			std::lock_guard<std::mutex> lock(m_mutex);
			std::string safe_filename = SanitizeFilename(filename);
			std::string filepath = m_config.archive_path + "/" + safe_filename;
			auto current_files = GetArchiveIndex(); // check file limit
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
				std::transform(lower_content.begin(), lower_content.end(), lower_content.begin(), [](int c) { return static_cast<char>(std::tolower(c)); });
				std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), [](int c) { return static_cast<char>(std::tolower(c)); });

				// Count keyword occurrences
				size_t pos = 0;
				while ((pos = lower_content.find(lower_query, pos)) != std::string::npos) {
					match_count++;
					pos++;
				}

				// Also check filename
				std::string lower_filename = filename;
				std::transform(lower_filename.begin(), lower_filename.end(), lower_filename.begin(), [](int c) { return static_cast<char>(std::tolower(c)); });
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
			return m_contextBuilder.BuildSystemPrompt(user_prompt);
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

		
		std::string MemoryManager::BuildNumberedContext() const {
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_contextBuilder.BuildNumberedContext();
		}

		// =====================================================
		// AI-Driven Context Modification
		// =====================================================

		bool MemoryManager::ApplyReplaceCommand(int32_t start_line, int32_t end_line, const std::string& replacement) {
			std::lock_guard<std::mutex> lock(m_mutex);

			// Validate line range
			if (start_line < 1 || end_line < start_line) {
				std::cerr << "[MEMORY] Invalid replace range: " << start_line << "-" << end_line << std::endl;
				return false;
			}

			// Count total lines in context
			int total_lines = 0;
			{
				std::istringstream stream(m_identity);
				std::string line;
				while (std::getline(stream, line)) total_lines++;
			}
			total_lines += 3; // section headers
			{
				std::istringstream stream(m_session_summary);
				std::string line;
				while (std::getline(stream, line)) total_lines++;
			}
			total_lines += 2; // section header + blank
			{
				std::istringstream stream(m_conversation);
				std::string line;
				while (std::getline(stream, line)) total_lines++;
			}
			total_lines += 2; // section header + blank

			if (start_line > total_lines) {
				std::cerr << "[MEMORY] Replace line " << start_line << " exceeds total lines " << total_lines << std::endl;
				return false;
			}

			std::cout << "[MEMORY] Replace lines " << start_line << "-" << end_line << ": "
			          << (replacement == "[DELETE]" ? "(deleted)" : "\"" + replacement.substr(0, 50) + (replacement.size() > 50 ? "..." : "") + "\"")
			          << std::endl;

			// Apply the replacement to the appropriate memory section
			// We need to map line numbers back to the correct section
			int current_line = 1;

			// Identity section: [1] header + identity lines
			int identity_start = current_line;
			current_line += 1; // header
			{
				std::vector<std::string> lines;
				std::istringstream stream(m_identity);
				std::string line;
				while (std::getline(stream, line)) lines.push_back(line);
				
				if (!lines.empty() || m_identity.empty()) {
					int identity_end = current_line + static_cast<int>(lines.size()) - 1;
					
					// Check if replace range overlaps with identity
					if (start_line <= identity_end && end_line >= identity_start) {
						std::vector<std::string> new_lines;
						for (size_t i = 0; i < lines.size(); ++i) {
							int line_num = identity_start + static_cast<int>(i);
							if (line_num >= start_line && line_num <= end_line) {
								// This line is in the replace range
								if (replacement != "[DELETE]") {
									new_lines.push_back(replacement);
								}
							} else {
								new_lines.push_back(lines[i]);
							}
						}
						
						// Rebuild identity
						m_identity.clear();
						for (size_t i = 0; i < new_lines.size(); ++i) {
							if (i > 0) m_identity += "\n";
							m_identity += new_lines[i];
						}
						SaveToDisk();
						return true;
					}
				}
				current_line += static_cast<int>(lines.size());
			}
			current_line += 1; // blank line after identity

			// Session section: [current] header + session lines
			int session_start = current_line;
			current_line += 1; // header
			{
				std::vector<std::string> lines;
				std::istringstream stream(m_session_summary);
				std::string line;
				while (std::getline(stream, line)) lines.push_back(line);
				
				if (!lines.empty() || m_session_summary.empty()) {
					int session_end = current_line + static_cast<int>(lines.size()) - 1;
					
					if (start_line <= session_end && end_line >= session_start) {
						std::vector<std::string> new_lines;
						for (size_t i = 0; i < lines.size(); ++i) {
							int line_num = session_start + static_cast<int>(i);
							if (line_num >= start_line && line_num <= end_line) {
								if (replacement != "[DELETE]") {
									new_lines.push_back(replacement);
								}
							} else {
								new_lines.push_back(lines[i]);
							}
						}
						
						m_session_summary.clear();
						for (size_t i = 0; i < new_lines.size(); ++i) {
							if (i > 0) m_session_summary += "\n";
							m_session_summary += new_lines[i];
						}
						SaveToDisk();
						return true;
					}
				}
				current_line += static_cast<int>(lines.size());
			}
			current_line += 1; // blank line after session

			// Conversation section: [current] header + conversation lines
			int conv_start = current_line;
			current_line += 1; // header
			{
				std::vector<std::string> lines;
				std::istringstream stream(m_conversation);
				std::string line;
				while (std::getline(stream, line)) lines.push_back(line);
				
				if (!lines.empty() || m_conversation.empty()) {
					int conv_end = current_line + static_cast<int>(lines.size()) - 1;
					
					if (start_line <= conv_end && end_line >= conv_start) {
						std::vector<std::string> new_lines;
						for (size_t i = 0; i < lines.size(); ++i) {
							int line_num = conv_start + static_cast<int>(i);
							if (line_num >= start_line && line_num <= end_line) {
								if (replacement != "[DELETE]") {
									new_lines.push_back(replacement);
								}
							} else {
								new_lines.push_back(lines[i]);
							}
						}
						
						m_conversation.clear();
						for (size_t i = 0; i < new_lines.size(); ++i) {
							if (i > 0) m_conversation += "\n";
							m_conversation += new_lines[i];
						}
						return true;
					}
				}
			}

			std::cerr << "[MEMORY] Replace range not found in any section" << std::endl;
			return false;
		}

		std::vector<std::pair<int32_t, int32_t>> MemoryManager::ExtractReplaceCommands(const std::string& text) const {
			std::vector<std::pair<int32_t, int32_t>> commands;
			
			// Match: replace X-Y with: content
			std::regex command_pattern(R"(replace\s+(\d+)-(\d+)\s+with:)");
			auto begin = std::sregex_iterator(text.begin(), text.end(), command_pattern);
			auto end = std::sregex_iterator();

			for (auto it = begin; it != end; ++it) {
				int32_t start = std::stoi(it->str(1));
				int32_t end_line = std::stoi(it->str(2));
				commands.push_back({start, end_line});
			}

			return commands;
		}

		std::string MemoryManager::FilterReplaceCommands(const std::string& text) const {
			// Remove the "replace X-Y with: ..." pattern from the text
			std::regex command_pattern(R"(replace\s+\d+-\d+\s+with:\s*[^\n]*\n?)");
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

		// =====================================================
		// Statement-Based Context System (v2)
		// =====================================================

		std::string MemoryManager::GenerateStatementID() {
			// Format: S{session}-{sequence} (zero-padded to 3 digits)
			char buf[32];
			std::snprintf(buf, sizeof(buf), "S%d-%03d", m_current_session, m_statement_sequence);
			m_statement_sequence++;
			return std::string(buf);
		}

		std::string MemoryManager::GetCurrentTimestamp() const {
			auto now = std::time(nullptr);
			struct tm tm_buf;
			localtime_s(&tm_buf, &now);
			char buf[64];
			std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
			return std::string(buf);
		}

		void MemoryManager::SetCurrentTimestamp(const std::string& timestamp) {
			m_current_timestamp = timestamp;
		}

		void MemoryManager::AppendStatement(const Statement& stmt) {
			m_statements.push_back(stmt);
		}

		int32_t MemoryManager::FindStatementByID(const std::string& id) const {
			for (int32_t i = 0; i < static_cast<int32_t>(m_statements.size()); i++) {
				if (m_statements[i].id == id) {
					return i;
				}
			}
			return -1;
		}

		std::pair<int32_t, int32_t> MemoryManager::FindStatementRange(const std::string& start_id, const std::string& end_id) const {
			int32_t start_idx = -1, end_idx = -1;

			// Parse session and sequence from IDs
			// Format: S{session}-{sequence}
			auto parseID = [](const std::string& id) -> std::pair<int32_t, int32_t> {
				// Find 'S' prefix, then session number, then '-', then sequence
				size_t dash_pos = id.find('-');
				if (dash_pos == std::string::npos || dash_pos == 0) {
					return {-1, -1};
				}
				int32_t session = std::stoi(id.substr(1, dash_pos - 1));
				int32_t sequence = std::stoi(id.substr(dash_pos + 1));
				return {session, sequence};
			};

			auto [start_session, start_seq] = parseID(start_id);
			auto [end_session, end_seq] = parseID(end_id);

			if (start_session < 0 || end_session < 0) {
				return {-1, -1};
			}

			for (int32_t i = 0; i < static_cast<int32_t>(m_statements.size()); i++) {
				auto [stmt_session, stmt_seq] = parseID(m_statements[i].id);
				if (stmt_session == start_session && stmt_seq == start_seq) {
					start_idx = i;
				}
				if (stmt_session == end_session && stmt_seq == end_seq) {
					end_idx = i;
				}
			}

			if (start_idx < 0 || end_idx < 0) {
				return {-1, -1};
			}

			// Validate: end must be >= start
			if (end_idx < start_idx) {
				return {-1, -1};
			}

			return {start_idx, end_idx};
		}

		bool MemoryManager::ApplyCompressCommand(const std::string& start_id, const std::string& end_id, const std::string& summary) {
			auto [start, end] = FindStatementRange(start_id, end_id);
			if (start < 0 || end < 0) {
				std::cerr << "[MEMORY] Compress failed: invalid range " << start_id << " to " << end_id << std::endl;
				return false;
			}

			// Count tokens saved
			int32_t original_tokens = 0;
			for (int32_t i = start; i <= end; i++) {
				original_tokens += CountTokensInternal(m_statements[i].content);
			}

			// Replace statements with summary
			Statement new_stmt;
			new_stmt.id = GenerateStatementID();
			new_stmt.timestamp = m_current_timestamp.empty() ? GetCurrentTimestamp() : m_current_timestamp;
			new_stmt.type = "SUMMARY";
			new_stmt.content = summary;

			// Remove old statements, add summary at start position
			m_statements.erase(m_statements.begin() + start, m_statements.begin() + end + 1);
			m_statements.insert(m_statements.begin() + start, new_stmt);

			std::cout << "[MEMORY] Compressed " << (end - start + 1) << " statements ("
			          << start_id << " to " << end_id << "). Saved ~" << original_tokens << " tokens." << std::endl;
			return true;
		}

		bool MemoryManager::ApplyDeleteCommand(const std::string& start_id, const std::string& end_id) {
			auto [start, end] = FindStatementRange(start_id, end_id);
			if (start < 0 || end < 0) {
				std::cerr << "[MEMORY] Delete failed: invalid range " << start_id << " to " << end_id << std::endl;
				return false;
			}

			int32_t count = end - start + 1;
			m_statements.erase(m_statements.begin() + start, m_statements.begin() + end + 1);

			std::cout << "[MEMORY] Deleted " << count << " statements ("
			          << start_id << " to " << end_id << ")." << std::endl;
			return true;
		}
		bool MemoryManager::ApplyCompressIDs(const std::vector<std::string>& ids, const std::string& summary) {
			if (ids.empty()) return false;
			std::vector<int32_t> indices;
			for (const auto& id : ids) {
				int32_t idx = FindStatementByID(id);
				if (idx < 0) {
					std::cerr << "[MEMORY] Compress failed: statement " << id << " not found" << std::endl;
					return false;
				}
				indices.push_back(idx);
			}
			std::sort(indices.begin(), indices.end());
			indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
			int32_t original_tokens = 0;
			for (int32_t idx : indices) {
				original_tokens += CountTokensInternal(m_statements[idx].content);
			}
			Statement new_stmt;
			new_stmt.id = GenerateStatementID();
			new_stmt.timestamp = m_current_timestamp.empty() ? GetCurrentTimestamp() : m_current_timestamp;
			new_stmt.type = "SUMMARY";
			new_stmt.content = summary;
			for (int32_t i = static_cast<int32_t>(indices.size()) - 1; i >= 0; --i) {
				m_statements.erase(m_statements.begin() + indices[i]);
			}
			m_statements.insert(m_statements.begin() + indices.front(), new_stmt);
			std::cout << "[MEMORY] Compressed " << indices.size() << " statements (";
			for (size_t i = 0; i < ids.size(); ++i) {
				if (i > 0) std::cout << ", ";
				std::cout << ids[i];
			}
			std::cout << "). Saved ~" << original_tokens << " tokens." << std::endl;
			return true;
		}

		bool MemoryManager::ApplyDeleteIDs(const std::vector<std::string>& ids) {
			if (ids.empty()) return false;
			std::vector<int32_t> indices;
			for (const auto& id : ids) {
				int32_t idx = FindStatementByID(id);
				if (idx < 0) {
					std::cerr << "[MEMORY] Delete failed: statement " << id << " not found" << std::endl;
					return false;
				}
				indices.push_back(idx);
			}
			std::sort(indices.begin(), indices.end());
			indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
			for (int32_t i = static_cast<int32_t>(indices.size()) - 1; i >= 0; --i) {
				m_statements.erase(m_statements.begin() + indices[i]);
			}
			std::cout << "[MEMORY] Deleted " << indices.size() << " statements (";
			for (size_t i = 0; i < ids.size(); ++i) {
				if (i > 0) std::cout << ", ";
				std::cout << ids[i];
			}
			std::cout << ")." << std::endl;
			return true;
		}

		std::string MemoryManager::BuildStatementContext() const {
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_contextBuilder.BuildStatementContext();
		}

		std::string MemoryManager::UpdateSystemStatus(const std::string& context, int32_t current_round) const {
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_contextBuilder.UpdateSystemStatus(context, current_round);
		}

		ReplaceCmdVec MemoryManager::ExtractStatementCommands(const std::string& text) const {
			ReplaceCmdVec commands;

			// Range patterns (with mandatory context_rewrite prefix)
			std::regex compress_range_pattern(R"(context_rewrite[ \t]+compress[ \t]+(S\d+-\d+)[ \t]+to[ \t]+(S\d+-\d+)[ \t]+with:[ \t]+(.+?))(?=\n|$)");
			std::regex delete_range_pattern(R"(context_rewrite[ \t]+delete[ \t]+(S\d+-\d+)[ \t]+to[ \t]+(S\d+-\d+))(?=\n|$)");
			// Comma-separated patterns (with mandatory context_rewrite prefix)
			std::regex compress_list_pattern(R"(context_rewrite[ \t]+compress[ \t]+((?:S\d+-\d+[ \t]*,[ \t]*)+S\d+-\d+)[ \t]+with:[ \t]+(.+?))(?=\n|$)");
			std::regex delete_list_pattern(R"(context_rewrite[ \t]+delete[ \t]+((?:S\d+-\d+[ \t]*,[ \t]*)+S\d+-\d+))(?=\n|$)");

			auto splitIDs = [](const std::string& ids_str) -> std::vector<std::string> {
				std::vector<std::string> result;
				std::istringstream stream(ids_str);
				std::string id;
				while (std::getline(stream, id, ',')) {
					size_t start = id.find_first_not_of(" \t");
					size_t end = id.find_last_not_of(" \t");
					if (start != std::string::npos && end != std::string::npos) {
						result.push_back(id.substr(start, end - start + 1));
					}
				}
				return result;
			};

			// Extract compress commands (comma-separated first, then range)
			auto begin = std::sregex_iterator(text.begin(), text.end(), compress_list_pattern);
			auto end = std::sregex_iterator();
			for (auto it = begin; it != end; ++it) {
				ReplaceCommand cmd;
				std::string ids_str = (*it)[1].str();
				if (ids_str.find(',') != std::string::npos) {
					cmd.statement_ids = splitIDs(ids_str);
				} else {
					cmd.start_id = ids_str;
				}
				cmd.summary = (*it)[2].str();
				cmd.is_delete = false;
				commands.push_back(cmd);
			}
			// Extract compress range commands
			begin = std::sregex_iterator(text.begin(), text.end(), compress_range_pattern);
			for (auto it = begin; it != end; ++it) {
				ReplaceCommand cmd;
				cmd.start_id = (*it)[1].str();
				cmd.end_id = (*it)[2].str();
				cmd.summary = (*it)[3].str();
				cmd.is_delete = false;
				commands.push_back(cmd);
			}

			// Extract delete commands (comma-separated first, then range)
			begin = std::sregex_iterator(text.begin(), text.end(), delete_list_pattern);
			end = std::sregex_iterator();
			for (auto it = begin; it != end; ++it) {
				ReplaceCommand cmd;
				std::string ids_str = (*it)[1].str();
				if (ids_str.find(',') != std::string::npos) {
					cmd.statement_ids = splitIDs(ids_str);
				} else {
					cmd.start_id = ids_str;
				}
				cmd.summary = "";
				cmd.is_delete = true;
				commands.push_back(cmd);
			}
			// Extract delete range commands
			begin = std::sregex_iterator(text.begin(), text.end(), delete_range_pattern);
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

		std::string MemoryManager::FilterStatementCommands(const std::string& text) const {
			std::regex compress_range_pattern(R"(context_rewrite[ \t]+compress[ \t]+S\d+-\d+[ \t]+to[ \t]+S\d+-\d+[ \t]+with:[ \t]*[^\n]*)");
			std::regex delete_range_pattern(R"(context_rewrite[ \t]+delete[ \t]+S\d+-\d+[ \t]+to[ \t]+S\d+-\d+)");
			std::regex compress_list_pattern(R"(context_rewrite[ \t]+compress[ \t]+(?:S\d+-\d+[ \t]*,[ \t]*)+S\d+-\d+[ \t]+with:[ \t]*[^\n]*)");
			std::regex delete_list_pattern(R"(context_rewrite[ \t]+delete[ \t]+(?:S\d+-\d+[ \t]*,[ \t]*)+S\d+-\d+)");
			std::string result = text;
			result = std::regex_replace(result, compress_range_pattern, "");
			result = std::regex_replace(result, delete_range_pattern, "");
			result = std::regex_replace(result, compress_list_pattern, "");
			result = std::regex_replace(result, delete_list_pattern, "");
			return result;
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