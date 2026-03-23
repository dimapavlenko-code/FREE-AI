#include "utils/Helpers.hpp"

std::string TrimNulls(const std::string& input) {
	size_t firstNull = input.find('\0');
	if (firstNull != std::string::npos) {
		return input.substr(0, firstNull);
	}
	return input;
}

std::string Trim(const std::string& str) {
	size_t start = str.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return "";
	size_t end = str.find_last_not_of(" \t\r\n");
	return str.substr(start, end - start + 1);
}