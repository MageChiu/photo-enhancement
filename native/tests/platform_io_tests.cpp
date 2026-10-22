#include "platform_io.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <atomic>

namespace {
void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
using File = std::unique_ptr<FILE, decltype(&std::fclose)>;
struct Directory {
  std::filesystem::path value;
  Directory():value(std::filesystem::temp_directory_path() /
      ("photo-io-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::filesystem::create_directory(value);
  }
  ~Directory(){std::error_code error;std::filesystem::remove_all(value,error);}
};
}
int main() {
  try {
    Directory directory;
    const auto filename=(directory.value / pe::io::path("中文 空格照片.dat")).u8string();
    {
      File file(pe::io::create_output(filename),std::fclose);
      check(bool(file),"create UTF-8 output");
      check(!pe::io::create_output(filename),"exclusive creation must not overwrite");
      check(std::fwrite("original",1,8,file.get())==8 && pe::io::flush(file.get()),"durable binary output");
    }
    {
      File file(pe::io::open_read(filename),std::fclose);
      char bytes[8]{};
      check(bool(file) && std::fread(bytes,1,8,file.get())==8 && std::memcmp(bytes,"original",8)==0,"read UTF-8 path and preserve original");
    }
    pe::io::remove(filename);
    check(!std::filesystem::exists(pe::io::path(filename)),"UTF-8 cleanup");
    File cache(pe::io::temporary(),std::fclose);
    check(bool(cache) && pe::io::resize(cache.get(),65536),"private resizable pixel cache");
    const std::array<unsigned char,4> a{0,10,13,255}, b{42,13,10,0};
    check(pe::io::write_at(cache.get(),a.data(),a.size(),0),"binary write at start");
    check(pe::io::write_at(cache.get(),b.data(),b.size(),32768),"binary write at offset");
    check(std::fflush(cache.get())==0,"flush pixel cache");
    std::atomic<bool> valid{true};
    auto reader=[&](const std::array<unsigned char,4>& expected,uint64_t offset){
      for(unsigned i=0;i<200;++i){std::array<unsigned char,4> bytes{};
        if(!pe::io::read_at(cache.get(),bytes.data(),bytes.size(),offset) || bytes!=expected)valid=false;}
    };
    std::thread first(reader,a,0),second(reader,b,32768);first.join();second.join();
    check(valid,"concurrent positional reads must not interfere");
    std::array<unsigned char,4> bytes{};
    check(!pe::io::read_at(cache.get(),bytes.data(),4,std::numeric_limits<uint64_t>::max()),"reject overflowing offset");
#ifndef _WIN32
    // Sparse-file check of offsets above 4 GiB without allocating image memory.
    const uint64_t offset=(uint64_t(1)<<32)+128;
    check(pe::io::resize(cache.get(),offset+4),"64-bit pixel-cache size");
    check(pe::io::write_at(cache.get(),a.data(),4,offset) && pe::io::read_at(cache.get(),bytes.data(),4,offset) && bytes==a,"64-bit positional I/O");
#endif
    std::cout << "Platform I/O checks passed\n";
    return 0;
  } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
