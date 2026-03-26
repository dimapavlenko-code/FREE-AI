#include "inference/LlamaContext.hpp"
#include <iostream>
#include <cstring>

// Include llama.cpp C API
#include "llama.h"

namespace FreeAI {
    namespace Inference {

        LlamaContext::LlamaContext()
            : m_model(nullptr)
            , m_ctx(nullptr)
            , m_sampler(nullptr)
            , m_vocab(nullptr)
            , m_memory_mgr(nullptr)
            , m_loaded(false) {
        }

        LlamaContext::~LlamaContext() {
            UnloadModel();
        }

        bool LlamaContext::Initialize(const ModelConfig& config, MemoryManager* memory_mgr) {
            if (m_loaded) {
                std::cerr << "[LLAMA] Model already loaded!" << std::endl;
                return false;
            }

            m_config = config;
            m_memory_mgr = memory_mgr;

            std::cout << "[LLAMA] Loading model: " << config.model_path << std::endl;

            // Initialize llama.cpp backend
            llama_backend_init();

            // Model parameters (n_ctx/n_threads moved to context_params)
            struct llama_model_params model_params = llama_model_default_params();
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

            // Check GPU support (unified function)
            if (config.use_gpu && llama_supports_gpu_offload()) {
                std::cout << "[LLAMA] GPU offload supported" << std::endl;
            }
            else if (config.use_gpu) {
                std::cout << "[LLAMA] WARNING: GPU offload requested but not supported" << std::endl;
            }

            // Load model (new API name)
            m_model = llama_model_load_from_file(config.model_path.c_str(), model_params);
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

        void LlamaContext::UnloadModel() {
            if (m_sampler) {
                llama_sampler_free(m_sampler);
                m_sampler = nullptr;
            }
            if (m_ctx) {
                llama_free(m_ctx);
                m_ctx = nullptr;
            }
            if (m_model) {
                llama_model_free(m_model);
                m_model = nullptr;
            }
            llama_backend_free();
            m_loaded = false;
            std::cout << "[LLAMA] Model unloaded" << std::endl;
        }

        bool LlamaContext::IsLoaded() const {
            return m_loaded;
        }

        std::string LlamaContext::GetModelInfo() const {
            if (!m_loaded) {
                return "No model loaded";
            }

            char buffer[256];
            snprintf(buffer, sizeof(buffer),
                "Context: %d tokens, Threads: %d, Batch: %d",
                m_config.n_ctx, m_config.n_threads, m_config.n_batch);
            return std::string(buffer);
        }

        std::vector<int> LlamaContext::Tokenize(const std::string& text) const {
            if (!m_loaded || !m_vocab) {
                return std::vector<int>();
            }

            std::vector<int> tokens(static_cast<int32_t>(text.size()) + 16);
            int32_t n_tokens = llama_tokenize(
                m_vocab,
                text.c_str(), static_cast<int32_t>(text.size()),
                tokens.data(), static_cast<int32_t>(tokens.size()),
                true, true  // add_special, parse_special
            );

            if (n_tokens < 0) {
                std::cerr << "[LLAMA] Tokenization failed" << std::endl;
                return std::vector<int>();
            }

            tokens.resize(static_cast<size_t>(n_tokens));
            return tokens;
        }

        std::string LlamaContext::Detokenize(const std::vector<int>& tokens) const {
            if (!m_loaded || !m_vocab || tokens.empty()) {
                return "";
            }

            std::string result;
            result.reserve(tokens.size() * 4);

            for (int token : tokens) {
                char buf[256];
                int32_t n_chars = llama_token_to_piece(
                    m_vocab,
                    token, buf, sizeof(buf), 0, true
                );
                if (n_chars > 0) {
                    result.append(buf, static_cast<size_t>(n_chars));
                }
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

            if (cmd == "/store") {
                m_memory_mgr->AppendToIdentity(arg);
                std::cout << "[MEMORY] Stored to identity: " << arg.substr(0, 50) << "..." << std::endl;
                return true;
            }
            else if (cmd == "/forget") {
                // Simple implementation - would need more sophisticated memory editing
                std::cout << "[MEMORY] Forget command received (implementation pending)" << std::endl;
                return true;
            }
            else if (cmd == "/write") {
                // Parse filename and content
                size_t content_pos = arg.find(' ');
                if (content_pos != std::string::npos) {
                    std::string filename = arg.substr(0, content_pos);
                    std::string content = arg.substr(content_pos + 1);
                    m_memory_mgr->WriteArchiveFile(filename, content);
                    std::cout << "[MEMORY] Wrote archive file: " << filename << std::endl;
                }
                return true;
            }
            else if (cmd == "/delete") {
                m_memory_mgr->DeleteArchiveFile(arg);
                std::cout << "[MEMORY] Deleted archive file: " << arg << std::endl;
                return true;
            }
            else if (cmd == "/summarize") {
                std::cout << "[MEMORY] Summarize command received (implementation pending)" << std::endl;
                return true;
            }

            return false;
        }

        std::string LlamaContext::Generate(const std::string& user_prompt) {
            if (!m_loaded) {
                return "Error: Model not loaded";
            }

            // Build system prompt with memory status
            std::string system_prompt = m_memory_mgr->BuildSystemPrompt(user_prompt);

            // Build full context (identity + session + conversation)
            std::string full_context = system_prompt + m_memory_mgr->BuildFullContext();

            // Add user prompt
            full_context += "<|im_start|>user\n" + user_prompt + "<|im_end|>\n<|im_start|>assistant\n";

            // Generate response
            std::string response = GenerateInternal(full_context);

            // Extract and execute commands
            auto commands = m_memory_mgr->ExtractCommands(response);
            for (const auto& cmd : commands) {
                ExecuteCommand(cmd);
            }

            // Filter commands from visible output
            std::string clean_response = m_memory_mgr->FilterCommands(response);

            // Add to conversation memory
            m_memory_mgr->AppendToConversation("User", user_prompt);
            m_memory_mgr->AppendToConversation("Assistant", clean_response);

            return clean_response;
        }

        std::string LlamaContext::GenerateInternal(const std::string& full_prompt) {
            // Tokenize prompt
            std::vector<int> prompt_tokens = Tokenize(full_prompt);
            if (prompt_tokens.empty()) {
                return "Error: Failed to tokenize prompt";
            }

            std::cout << "[LLAMA] Prompt tokens: " << prompt_tokens.size() << std::endl;

            // Decode prompt (process all tokens at once)
            struct llama_batch batch = llama_batch_get_one(prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size()));
            if (llama_decode(m_ctx, batch) != 0) {
                std::cerr << "[LLAMA] Failed to decode prompt" << std::endl;
                return "Error: Failed to process prompt";
            }

            // Generate response
            std::vector<int> generated_tokens;
            int32_t n_predict = m_config.n_predict;

            for (int32_t i = 0; i < n_predict; ++i) {
                // Sample next token
                llama_token new_token_id = llama_sampler_sample(m_sampler, m_ctx, -1);

                // Check for end of generation
                if (llama_vocab_is_eog(m_vocab, new_token_id)) {
                    std::cout << "[LLAMA] End of generation token" << std::endl;
                    break;
                }

                generated_tokens.push_back(new_token_id);

                // Decode generated token
                batch = llama_batch_get_one(&new_token_id, 1);
                if (llama_decode(m_ctx, batch) != 0) {
                    std::cerr << "[LLAMA] Failed to decode generated token" << std::endl;
                    break;
                }
            }

            // Detokenize generated tokens
            std::string response = Detokenize(generated_tokens);
            std::cout << "[LLAMA] Generated " << generated_tokens.size() << " tokens" << std::endl;

            return response;
        }

    }
}