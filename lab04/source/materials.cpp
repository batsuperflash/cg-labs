#include "materials.hpp"

#include <algorithm>
#include <map>
#include <string_view>
#include <utility>

namespace materials {

namespace {

bool endsWith(const std::string& text, std::string_view suffix) {
	return text.size() > suffix.size() && std::string_view(text).substr(text.size() - suffix.size()) == suffix;
}

} // namespace

std::vector<Files> discover(const std::filesystem::path& directory) {
	std::map<std::string, Files> found;

	std::error_code error;
	for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
		if (!entry.is_regular_file()) {
			continue;
		}

		const std::string stem = entry.path().stem().string();
		for (const auto& [suffix, member] : { std::pair{ std::string_view("_albedo"), &Files::albedo },
		                                      std::pair{ std::string_view("_specular"), &Files::specular },
		                                      std::pair{ std::string_view("_emissive"), &Files::emissive } }) {
			if (endsWith(stem, suffix)) {
				const std::string name = stem.substr(0, stem.size() - suffix.size());
				found[name].name = name;
				found[name].*member = entry.path();
			}
		}
	}

	// std::map уже хранит материалы по возрастанию имени, так что порядок не зависит от порядка файлов в папке.
	// Карты блика и свечения без альбедо материала не создают.
	std::vector<Files> result;
	for (auto& [name, files] : found) {
		if (!files.albedo.empty()) {
			result.push_back(std::move(files));
		}
	}
	return result;
}

} // namespace materials
