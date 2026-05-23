#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

std::string JsonEscape(const std::string& text);

std::optional<std::string> JsonGetString(const std::string& json, const std::string& key, size_t from = 0);

std::optional<int64_t> JsonGetInt(const std::string& json, const std::string& key, size_t from = 0);

std::optional<double> JsonGetDouble(const std::string& json, const std::string& key, size_t from = 0);

std::vector<std::string> JsonSplitTopObjects(const std::string& json);
