#include "inference/LlamaContext.hpp"
#include <iostream>
#include <cstring>
#include <regex>
#include <ctime>
#include "llama.h" // llama.cpp C API

namespace FreeAI {
    namespace Inference {

        // System prompt
	static const char* k_system_prompt = R"(You are a helpful AI assistant with memory management capabilities.
You can manage your context memory using the following commands:

1. Compress statements: context_rewrite compress S1-001 to S1-005 with: summary of the compressed content
	  or context_rewrite compress S1-001, S1-002, S1-003 with: summary of the compressed content
2. Delete statements: context_rewrite delete S1-001 to S1-003
	  or context_rewrite delete S1-001, S1-002, S1-003

When you need to compress or delete old statements to free memory, use these commands in your response.
The system will automatically execute them and inject the results as tool reports.

Current system status:
- Round: 0/20
- Statements: 0
- Memory: OK
)";



        LlamaContext::LlamaContext()
            : m_model(nullptr), m_ctx(nullptr), m_sampler(nullptr), m_vocab(nullptr)
            , m_memory_mgr(nullptr), m_loaded(false) {}

        LlamaContext::~LlamaContext() { UnloadModel(); }

        bool LlamaContext::Initialize(const ModelConfig& config, MemoryManager* memory_mgr) {
            if (m_loaded) {
                std::cerr << "[LLAMA] Model already loaded!" << std::endl;
                return false;
            }
            m_config = config;
            m_memory_mgr = memory_mgr;
            std::cout << "[LLAMA] Loading model: " << config.model_path << std::endl;
            llama_backend_init(); // init backend
            struct llama_model_params model_params = llama_model_default_params(); // model params
            model_params.n_gpu_layers = config.use_gpu ? config.n_gpu_layers : 0;
            model_params.use_mmap = config.use_mmap;
            model_params.use_mlock = config.use_mlock;
            if (config.use_gpu) {
                std::cout << "[LLAMA] GPU offloading enabled: "
                    << (config.n_gpu_layers == -1 ? "ALL layers" :
                        std::to_string(config.n_gpu_layers) + " layers")
                    << std::endl;
            }
            else {
                std::cout << "[LLAMA] GPU offloading disabled (CPU only)" << std::endl;
            }
            if (config.use_gpu && llama_supports_gpu_offload()) { // check GPU support
                std::cout << "[LLAMA] GPU offload supported" << std::endl;
            }
            else if (config.use_gpu) {
                std::cout << "[LLAMA] WARNING: GPU offload requested but not supported" << std::endl;
            }
            m_model = llama_model_load_from_file(config.model_path.c_str(), model_params); // load model
            if (!m_model) {
                std::cerr << "[LLAMA] Failed to load model: " << config.model_path << std::endl;
                llama_backend_free();
                return false;
            }

            // Get vocab
            m_vocab = llama_model_get_vocab(m_model);

            // Context parameters (n_ctx, n_threads moved HERE)
            struct llama_context_params ctx_params = llama_context_default_params();
            ctx_params.n_ctx = config.n_ctx;
            ctx_params.n_batch = config.n_batch;
            ctx_params.n_threads = config.n_threads;
            ctx_params.n_threads_batch = config.n_threads;

            // Create context (new API name)
            m_ctx = llama_init_from_model(m_model, ctx_params);
            if (!m_ctx) {
                std::cerr << "[LLAMA] Failed to create context" << std::endl;
                llama_model_free(m_model);
                m_model = nullptr;
                llama_backend_free();
                return false;
            }

            // Create sampler chain
            struct llama_sampler_chain_params sampler_params = llama_sampler_chain_default_params();
            m_sampler = llama_sampler_chain_init(sampler_params);

            // Add samplers (temperature + dist)
            llama_sampler_chain_add(m_sampler, llama_sampler_init_temp(config.temperature));
            llama_sampler_chain_add(m_sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

            m_loaded = true;

            std::cout << "[LLAMA] Model loaded successfully!" << std::endl;
            std::cout << "[LLAMA] " << GetModelInfo() << std::endl;
            return true;
        }

        void LlamaContext::UnloadModel() { // unload model
            if (m_sampler) { llama_sampler_free(m_sampler); m_sampler = nullptr; }
            if (m_ctx) { llama_free(m_ctx); m_ctx = nullptr; }
            if (m_model) { llama_model_free(m_model); m_model = nullptr; }
            llama_backend_free();
            m_loaded = false;
            std::cout << "[LLAMA] Model unloaded" << std::endl;
        }

        bool LlamaContext::IsLoaded() const { return m_loaded; }

        std::string LlamaContext::GetModelInfo() const {
            if (!m_loaded) return "No model loaded";
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "Context: %d tokens, Threads: %d, Batch: %d",
                m_config.n_ctx, m_config.n_threads, m_config.n_batch);
            return std::string(buffer);
        }

        std::vector<int> LlamaContext::Tokenize(const std::string& text) const { // tokenize text
            if (!m_loaded || !m_vocab) return std::vector<int>();
            std::vector<int> tokens(static_cast<int32_t>(text.size()) + 16);
            int32_t n_tokens = llama_tokenize(m_vocab, text.c_str(), static_cast<int32_t>(text.size()),
                tokens.data(), static_cast<int32_t>(tokens.size()), true, true); // add_special, parse_special
            if (n_tokens < 0) {
                std::cerr << "[LLAMA] Tokenization failed" << std::endl;
                return std::vector<int>();
            }
            tokens.resize(static_cast<size_t>(n_tokens));
            return tokens;
        }

        std::string LlamaContext::Detokenize(const std::vector<int>& tokens) const { // detokenize tokens
            if (!m_loaded || !m_vocab || tokens.empty()) return "";
            std::string result;
            result.reserve(tokens.size() * 4);
            for (int token : tokens) {
                char buf[256];
                int32_t n_chars = llama_token_to_piece(m_vocab, token, buf, sizeof(buf), 0, true);
                if (n_chars > 0) result.append(buf, static_cast<size_t>(n_chars));
            }
            return result;
        }

        bool LlamaContext::ExecuteCommand(const std::string& command) {
            if (!m_memory_mgr) {
                return false;
            }

            // Parse command
            size_t space_pos = command.find(' ');
            std::string cmd = command.substr(0, space_pos);
            std::string arg = (space_pos != std::string::npos) ? command.substr(space_pos + 1) : "";
            if (cmd == "/store") { // store to identity
                m_memory_mgr->AppendToIdentity(arg);
                std::cout << "[MEMORY] Stored to identity: " << arg.substr(0, 50) << "..." << std::endl;
                return true;
            }
            else if (cmd == "/forget") { // forget (pending)
                std::cout << "[MEMORY] Forget command received (pending)" << std::endl;
                return true;
            }
            else if (cmd == "/write") { // write to archive
                size_t content_pos = arg.find(' ');
                if (content_pos != std::string::npos) {
                    std::string filename = arg.substr(0, content_pos);
                    std::string content = arg.substr(content_pos + 1);
                    m_memory_mgr->WriteArchiveFile(filename, content);
                    std::cout << "[MEMORY] Wrote archive: " << filename << std::endl;
                }
                return true;
            }
            else if (cmd == "/delete") { // delete archive
                m_memory_mgr->DeleteArchiveFile(arg);
                std::cout << "[MEMORY] Deleted archive: " << arg << std::endl;
                return true;
            }
            else if (cmd == "/summarize") { // summarize (pending)
                std::cout << "[MEMORY] Summarize command received (pending)" << std::endl;
                return true;
            }
            return false;
        }

        // Inject tool report after command execution
        static void InjectReportStatement(MemoryManager* memory_mgr, const std::string& command, bool success) {
            if (!memory_mgr) return;
            Statement report;
            report.id = memory_mgr->GenerateStatementID();
            report.timestamp = memory_mgr->GetCurrentTimestamp();
            report.type = success ? "TOOL_REPORT" : "TOOL_ERROR";
            report.content = success ? "Successfully executed: " + command : "Failed to execute: " + command;
            memory_mgr->AppendStatement(report);
        }

        std::string LlamaContext::Generate(const std::string& user_prompt) {
            if (!m_loaded) return "Error: Model not loaded";
            // Init statement system for new round
            if (m_memory_mgr) {
                m_memory_mgr->SetCurrentRound(0);
                m_memory_mgr->SetCurrentTimestamp(std::to_string(std::time(nullptr)));
            }
            // Use statement-based context
            std::string context = m_memory_mgr ? m_memory_mgr->BuildStatementContext() : "";
            context = m_memory_mgr ? m_memory_mgr->UpdateSystemStatus(context, 0) : "";
            std::string full_context = k_system_prompt + context + "User: " + user_prompt + "\nAssistant: ";
            // Re-invocation loop (max 20 housekeeping rounds)
            const int32_t max_rounds = 20;
            std::string last_response;
            for (int32_t round = 0; round < max_rounds; round++) {
                context = m_memory_mgr ? m_memory_mgr->BuildStatementContext() : "";
                context = m_memory_mgr ? m_memory_mgr->UpdateSystemStatus(context, round) : "";
                full_context = k_system_prompt + context + "User: " + user_prompt + "\nAssistant: ";
                last_response = GenerateInternal(full_context); // gen response
                ReplaceCmdVec commands;
                if (m_memory_mgr) commands = m_memory_mgr->ExtractStatementCommands(last_response);
                if (commands.empty()) break;
           
                for (const auto& cmd : commands) {
                       	bool success = false;
                       	
                       	if (!cmd.statement_ids.empty()) {
                       		if (cmd.is_delete) {
                       			success = m_memory_mgr ? m_memory_mgr->ApplyDeleteIDs(cmd.statement_ids) : false;
                       		}
                       		else if (!cmd.summary.empty()) {
                       			success = m_memory_mgr ? m_memory_mgr->ApplyCompressIDs(cmd.statement_ids, cmd.summary) : false;
                       		}
                       	}
                       	else if (cmd.is_delete) {
                       		success = m_memory_mgr ? m_memory_mgr->ApplyDeleteCommand(cmd.start_id, cmd.end_id) : false;
                       	}
                       	else if (!cmd.summary.empty()) {
                       		success = m_memory_mgr ? m_memory_mgr->ApplyCompressCommand(cmd.start_id, cmd.end_id, cmd.summary) : false;
                       	}
                       	
                       	std::string cmd_desc;
                       	if (!cmd.statement_ids.empty()) {
                       		cmd_desc = (cmd.is_delete ? "delete " : "compress ") + cmd.statement_ids[0];
                       		for (size_t i = 1; i < cmd.statement_ids.size(); ++i) {
                       			cmd_desc += ", " + cmd.statement_ids[i];
                       		}
                       	} else {
                       		cmd_desc = (cmd.is_delete ? "delete " : "compress ") + cmd.start_id + " to " + cmd.end_id;
                       	}
                       	InjectReportStatement(m_memory_mgr, cmd_desc, success);
                       }
                if (m_memory_mgr) m_memory_mgr->SetCurrentRound(round + 1); // update round counter
                if (last_response.find("[CONTINUE]") != std::string::npos) continue; // continue generating
                break; // no more commands/continuation, exit loop
            }
            // Extract clean response (filter out statement commands)
            std::string clean_response = m_memory_mgr ? m_memory_mgr->FilterStatementCommands(last_response) : last_response;
            // Add to conversation memory as statements
            if (m_memory_mgr) {
                Statement user_stmt;
                user_stmt.id = m_memory_mgr->GenerateStatementID();
                user_stmt.timestamp = m_memory_mgr->GetCurrentTimestamp();
                user_stmt.type = "USER";
                user_stmt.content = user_prompt;
                m_memory_mgr->AppendStatement(user_stmt);
                Statement assistant_stmt;
                assistant_stmt.id = m_memory_mgr->GenerateStatementID();
                assistant_stmt.timestamp = m_memory_mgr->GetCurrentTimestamp();
                assistant_stmt.type = "ASSISTANT";
                assistant_stmt.content = clean_response;
                m_memory_mgr->AppendStatement(assistant_stmt);
            }
            return clean_response;
        }

        std::string LlamaContext::GenerateInternal(const std::string& full_prompt) { // gen internal
            std::vector<int> prompt_tokens = Tokenize(full_prompt); // tokenize prompt
            if (prompt_tokens.empty()) return "Error: Failed to tokenize prompt";
            std::cout << "[LLAMA] Prompt tokens: " << prompt_tokens.size() << std::endl;
            struct llama_batch batch = llama_batch_get_one(prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size())); // decode prompt
            if (llama_decode(m_ctx, batch) != 0) {
                std::cerr << "[LLAMA] Failed to decode prompt" << std::endl;
                return "Error: Failed to process prompt";
            }
            std::vector<int> generated_tokens;
            int32_t n_predict = m_config.n_predict;
            for (int32_t i = 0; i < n_predict; ++i) {
                llama_token new_token_id = llama_sampler_sample(m_sampler, m_ctx, -1); // sample next token
                if (llama_vocab_is_eog(m_vocab, new_token_id)) { // check for end of generation
                    std::cout << "[LLAMA] End of generation token" << std::endl;
                    break;
                }
                generated_tokens.push_back(new_token_id);
                batch = llama_batch_get_one(&new_token_id, 1); // decode generated token
                if (llama_decode(m_ctx, batch) != 0) {
                    std::cerr << "[LLAMA] Failed to decode generated token" << std::endl;
                    break;
                }
            }
            std::string response = Detokenize(generated_tokens); // detokenize generated tokens
            std::cout << "[LLAMA] Generated " << generated_tokens.size() << " tokens" << std::endl;
            return response;
        }

    }
}
