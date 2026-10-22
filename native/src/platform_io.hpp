#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

namespace pe::io {
// Public C ABI paths are UTF-8 on every platform.
std::filesystem::path path(const std::string& utf8);
FILE* open_read(const std::string& utf8);
FILE* create_output(const std::string& utf8); // exclusive, binary; never overwrites
void remove(const std::string& utf8) noexcept;
bool flush(FILE* file);
FILE* temporary(); // private, binary; automatically removed on close
bool resize(FILE* file, uint64_t bytes);
bool read_at(FILE* file, void* data, size_t bytes, uint64_t offset);
bool write_at(FILE* file, const void* data, size_t bytes, uint64_t offset);
bool local_time(const std::time_t* value, std::tm* result);
}
