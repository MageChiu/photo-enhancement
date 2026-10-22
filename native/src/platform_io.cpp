#include "platform_io.hpp"
#include <limits>
#include <mutex>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <random>
#include <sstream>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace pe::io {
std::filesystem::path path(const std::string& utf8) {
  return std::filesystem::u8path(utf8);
}
FILE* open_read(const std::string& utf8) {
#ifdef _WIN32
  return _wfopen(path(utf8).c_str(), L"rb");
#else
  return std::fopen(utf8.c_str(), "rb");
#endif
}
FILE* create_output(const std::string& utf8) {
#ifdef _WIN32
  const int fd = _wopen(path(utf8).c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
      _S_IREAD | _S_IWRITE);
  if (fd < 0) return nullptr;
  FILE* file = _fdopen(fd, "wb");
  if (!file) { _close(fd); remove(utf8); }
#else
  const int fd = ::open(utf8.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (fd < 0) return nullptr;
  FILE* file = fdopen(fd, "wb");
  if (!file) { ::close(fd); remove(utf8); }
#endif
  return file;
}
void remove(const std::string& utf8) noexcept {
  try { std::error_code error; std::filesystem::remove(path(utf8), error); }
  catch (...) {} // cleanup must not hide the original error
}
bool flush(FILE* file) {
  if (std::fflush(file) != 0) return false;
#ifdef _WIN32
  return _commit(_fileno(file)) == 0;
#else
  return ::fsync(fileno(file)) == 0;
#endif
}
FILE* temporary() {
#ifdef _WIN32
  // Atomic CREATE_NEW plus delete-on-close; no discover/open race or shared handle.
  std::random_device random;
  for (unsigned attempt = 0; attempt < 32; ++attempt) {
    std::wostringstream name;
    name << L"photo-pixels-" << std::hex << random() << random() << random() << L".tmp";
    const auto filename = std::filesystem::temp_directory_path() / name.str();
    HANDLE handle = CreateFileW(filename.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) continue;
      return nullptr;
    }
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDWR | _O_BINARY);
    if (fd < 0) { CloseHandle(handle); return nullptr; }
    FILE* file = _fdopen(fd, "w+b");
    if (!file) _close(fd);
    return file;
  }
  return nullptr;
#else
  const auto filename = (std::filesystem::temp_directory_path() / "photo-pixels-XXXXXX").string();
  std::vector<char> writable(filename.begin(), filename.end()); writable.push_back(0);
  const int fd = mkstemp(writable.data());
  if (fd < 0) return nullptr;
  ::unlink(writable.data());
  FILE* file = fdopen(fd, "w+b");
  if (!file) ::close(fd);
  return file;
#endif
}
bool resize(FILE* file, uint64_t bytes) {
  if (bytes > uint64_t(std::numeric_limits<int64_t>::max()) || std::fflush(file) != 0) return false;
#ifdef _WIN32
  return _chsize_s(_fileno(file), bytes) == 0;
#else
  return ftruncate(fileno(file), static_cast<off_t>(bytes)) == 0;
#endif
}
#ifdef _WIN32
// Position + I/O must be atomic with respect to other positional users of FILE*.
// All pixel-cache reads/writes use these helpers; sequential writes finish first.
static std::mutex positional_io;
#endif
bool read_at(FILE* file, void* data, size_t bytes, uint64_t offset) {
  if (offset > uint64_t(std::numeric_limits<int64_t>::max())) return false;
#ifdef _WIN32
  const std::lock_guard<std::mutex> lock(positional_io);
  return _fseeki64(file, static_cast<int64_t>(offset), SEEK_SET) == 0 &&
      std::fread(data, 1, bytes, file) == bytes;
#else
  return pread(fileno(file), data, bytes, static_cast<off_t>(offset)) == static_cast<ssize_t>(bytes);
#endif
}
bool write_at(FILE* file, const void* data, size_t bytes, uint64_t offset) {
  if (offset > uint64_t(std::numeric_limits<int64_t>::max())) return false;
#ifdef _WIN32
  const std::lock_guard<std::mutex> lock(positional_io);
  return _fseeki64(file, static_cast<int64_t>(offset), SEEK_SET) == 0 &&
      std::fwrite(data, 1, bytes, file) == bytes;
#else
  return pwrite(fileno(file), data, bytes, static_cast<off_t>(offset)) == static_cast<ssize_t>(bytes);
#endif
}
bool local_time(const std::time_t* value, std::tm* result) {
#ifdef _WIN32
  return localtime_s(result, value) == 0;
#else
  return localtime_r(value, result) != nullptr;
#endif
}
}
