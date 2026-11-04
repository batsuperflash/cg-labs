#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Поиск материалов в папке с текстурами. Не зависит от Vulkan, поэтому проверяется в lab_tests.
namespace materials {

// Материал — это набор файлов с общим именем: <имя>_albedo.<расширение> (обязателен), <имя>_specular.<расширение>
// и <имя>_emissive.<расширение>. Пустой путь означает, что файла нет.
struct Files {
	std::string name;
	std::filesystem::path albedo;
	std::filesystem::path specular;
	std::filesystem::path emissive;
};

// Просматривает папку и возвращает материалы, отсортированные по имени. Файл без альбедо материалом не считается.
std::vector<Files> discover(const std::filesystem::path& directory);

} // namespace materials
