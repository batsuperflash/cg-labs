#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Поиск материалов в папке с текстурами. Не зависит от Vulkan, поэтому проверяется в lab_tests.
namespace materials {

// Материал — это набор файлов с общим именем: <имя>_albedo.<расширение>. Пустой путь означает, что файла нет.
struct Files {
	std::string name;
	std::filesystem::path albedo;
};

// Просматривает папку и возвращает материалы, отсортированные по имени. Файл без суффикса _albedo материалом не считается.
std::vector<Files> discover(const std::filesystem::path& directory);

} // namespace materials
