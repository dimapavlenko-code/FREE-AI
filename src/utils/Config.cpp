#include "utils/Config.hpp"
#include <algorithm>
#include <cctype>

namespace FreeAI {
    namespace Utils {       

        ConfigSection* Config::FindSection(const std::string& name) {
            for (auto& section : m_sections) {
                if (section.name == name) return &section;
            }
            return nullptr;
        }

        const ConfigSection* Config::FindSection(const std::string& name) const {
            for (const auto& section : m_sections) {
                if (section.name == name) return &section;
            }
            return nullptr;
        }

        ConfigEntry* Config::FindEntry(ConfigSection& section, const std::string& key) {
            for (auto& entry : section.entries) {
                if (entry.key == key) return &entry;
            }
            return nullptr;
        }

        const ConfigEntry* Config::FindEntry(const ConfigSection& section, const std::string& key) const {
            for (const auto& entry : section.entries) {
                if (entry.key == key) return &entry;
            }
            return nullptr;
        }

        bool Config::Load(const std::string& filename) {
            std::ifstream file(filename);
            if (!file.is_open()) {
                return false;
            }

            m_sections.clear();
            std::string line;
            std::string pendingComment;
            ConfigSection* currentSection = nullptr;

            while (std::getline(file, line)) {
                std::string trimmedLine = Trim(line);

                // Empty line - preserve as spacing
                if (trimmedLine.empty()) {
                    continue;
                }

                // Comment line
                if (trimmedLine[0] == ';' || trimmedLine[0] == '#') {
                    pendingComment = trimmedLine;
                    continue;
                }

                // Section header [section]
                if (trimmedLine.front() == '[' && trimmedLine.back() == ']') {
                    std::string sectionName = trimmedLine.substr(1, trimmedLine.size() - 2);

                    // Check if section already exists
                    currentSection = FindSection(sectionName);
                    if (!currentSection) {
                        ConfigSection newSection;
                        newSection.name = sectionName;
                        newSection.comment = pendingComment;
                        m_sections.push_back(newSection);
                        currentSection = &m_sections.back();
                    }
                    pendingComment.clear();
                    continue;
                }

                // Key=Value (with multiline support)
                size_t pos = trimmedLine.find('=');
                if (pos != std::string::npos) {
                    if (!currentSection) {
                        // Key outside section - create default section
                        ConfigSection defaultSection;
                        defaultSection.name = "";
                        m_sections.push_back(defaultSection);
                        currentSection = &m_sections.back();
                    }

                    std::string key = Trim(trimmedLine.substr(0, pos));
                    std::string value = Trim(trimmedLine.substr(pos + 1));

                    // Check for multiline value (PEM keys)
                    // If value starts with "-----BEGIN", read until "-----END"
                    if (value.find("-----BEGIN") != std::string::npos) {
                        std::string fullValue = value;
                        while (std::getline(file, line)) {
                            std::string trimmed = Trim(line);
                            fullValue += "\n" + line; // Preserve original line (with indentation)
                            if (trimmed.find("-----END") != std::string::npos) {
                                break;
                            }
                        }
                        value = fullValue;
                    }

                    // Check if key already exists
                    ConfigEntry* existing = FindEntry(*currentSection, key);
                    if (existing) {
                        existing->value = value;
                        existing->comment = pendingComment;
                    }
                    else {
                        ConfigEntry newEntry;
                        newEntry.key = key;
                        newEntry.value = value;
                        newEntry.comment = pendingComment;
                        currentSection->entries.push_back(newEntry);
                    }
                    pendingComment.clear();
                }
            }

            file.close();
            return true;
        }

        bool Config::Save(const std::string& filename) const {
            std::ofstream file(filename);
            if (!file.is_open()) {
                return false;
            }

            for (size_t i = 0; i < m_sections.size(); ++i) {
                const auto& section = m_sections[i];

                // Write section comment
                if (!section.comment.empty()) {
                    file << section.comment << std::endl;
                }

                // Write section header (skip for default empty section)
                if (!section.name.empty()) {
                    file << "[" << section.name << "]" << std::endl;
                }

                // Write entries
                for (const auto& entry : section.entries) {
                    // Write entry comment
                    if (!entry.comment.empty()) {
                        file << entry.comment << std::endl;
                    }

                    // Write key=value (handle multiline)
                    file << entry.key << " = " << entry.value << std::endl;
                }

                // Add blank line between sections
                if (i < m_sections.size() - 1) {
                    file << std::endl;
                }
            }

            file.close();
            return true;
        }

        std::string Config::Get(const std::string& section, const std::string& key, const std::string& defaultVal) const {
            const ConfigSection* sec = FindSection(section);
            if (sec) {
                const ConfigEntry* entry = FindEntry(*sec, key);
                if (entry) {
                    return entry->value;
                }
            }
            return defaultVal;
        }

        int Config::GetInt(const std::string& section, const std::string& key, int defaultVal) const {
            try {
                return std::stoi(Get(section, key, std::to_string(defaultVal)));
            }
            catch (...) {
                return defaultVal;
            }
        }

        bool Config::GetBool(const std::string& section, const std::string& key, bool defaultVal) const {
            std::string val = Get(section, key, defaultVal ? "true" : "false");
            return (val == "true" || val == "1" || val == "yes");
        }

        void Config::Set(const std::string& section, const std::string& key, const std::string& value) {
            ConfigSection* sec = FindSection(section);
            if (!sec) {
                ConfigSection newSection;
                newSection.name = section;
                m_sections.push_back(newSection);
                sec = &m_sections.back();
            }

            ConfigEntry* entry = FindEntry(*sec, key);
            if (entry) {
                entry->value = value;
            }
            else {
                ConfigEntry newEntry;
                newEntry.key = key;
                newEntry.value = value;
                sec->entries.push_back(newEntry);
            }
        }

        void Config::SetInt(const std::string& section, const std::string& key, int value) {
            Set(section, key, std::to_string(value));
        }

        void Config::SetBool(const std::string& section, const std::string& key, bool value) {
            Set(section, key, value ? "true" : "false");
        }

        void Config::SetComment(const std::string& section, const std::string& key, const std::string& comment) {
            ConfigSection* sec = FindSection(section);
            if (!sec) return;

            ConfigEntry* entry = FindEntry(*sec, key);
            if (entry) {
                entry->comment = comment;
            }
        }

        void Config::SetSectionComment(const std::string& section, const std::string& comment) {
            ConfigSection* sec = FindSection(section);
            if (!sec) {
                ConfigSection newSection;
                newSection.name = section;
                m_sections.push_back(newSection);
                sec = &m_sections.back();
            }
            sec->comment = comment;
        }

    }
}