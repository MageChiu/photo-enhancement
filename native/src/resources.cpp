#include "internal.hpp"
#include <unistd.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#if TARGET_OS_IPHONE
#include <os/proc.h>
#endif
#endif

namespace pe {
uint64_t memory_budget(bool preview) noexcept {
  constexpr uint64_t MiB = 1024ULL * 1024;
  uint64_t physical = 0, available = 0;
#if defined(__APPLE__)
  size_t length = sizeof(physical);
  if (sysctlbyname("hw.memsize", &physical, &length, nullptr, 0) != 0) physical = 0;
  vm_statistics64_data_t stats{};
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  const auto host = mach_host_self();
  const long page_size = sysconf(_SC_PAGESIZE);
  if (page_size > 0 && host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &count) == KERN_SUCCESS)
    available = (uint64_t(stats.free_count) + stats.inactive_count) * uint64_t(page_size);
  mach_port_deallocate(mach_task_self(), host);
#if TARGET_OS_IPHONE
  const uint64_t headroom = os_proc_available_memory();
  if (headroom) available = available ? std::min(available, headroom) : headroom;
#endif
#else
  const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
  if (pages > 0 && page > 0) physical = uint64_t(pages) * uint64_t(page);
  if (FILE* file = std::fopen("/proc/meminfo", "r")) {
    char line[256]; unsigned long long kb = 0;
    while (std::fgets(line, sizeof(line), file))
      if (std::sscanf(line, "MemAvailable: %llu kB", &kb) == 1) { available = uint64_t(kb) * 1024; break; }
    std::fclose(file);
  }
#endif
#if defined(__ANDROID__) || defined(__OHOS__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  uint64_t budget = physical ? std::min(physical / 8, 512 * MiB) : 128 * MiB;
  if (available) budget = std::min(budget, available / 3);
#else
  uint64_t budget = physical ? physical / 4 : 256 * MiB;
  if (available) budget = std::min(budget, available / 2);
#endif
  return preview ? std::min(budget, 128 * MiB) : budget;
}

void require_memory(uint64_t bytes, uint64_t budget, const std::string& stage) {
  require(bytes <= budget, stage + "预计需要 " + std::to_string((bytes + 1048575) / 1048576) +
      " MiB，当前处理预算为 " + std::to_string(budget / 1048576) +
      " MiB。请释放内存后重试，或选择较小的源图。", PE_LIMIT_EXCEEDED);
}

DiskImage::~DiskImage() { if (file) std::fclose(file); }
std::unique_ptr<pe_image> DiskImage::strip(uint32_t y, uint32_t rows) const {
  const auto& info = metadata.info;
  const bool transpose = metadata.orientation >= 5;
  const uint32_t width = transpose ? info.height : info.width, height = transpose ? info.width : info.height;
  require(y < height && rows <= height - y, "Invalid disk strip");
  auto out = image(width, rows);
  out->info = info; out->info.width = width; out->info.height = rows;
  // A transposed strip maps to a narrow rectangle in source coordinates.
  uint32_t sx0 = 0, sx1 = info.width, sy0 = y, sy1 = y + rows;
  switch (metadata.orientation) {
    case 3: case 4: sy0 = info.height-y-rows; sy1 = info.height-y; break;
    case 5: case 6: sx0 = y; sx1 = y+rows; sy0 = 0; sy1 = info.height; break;
    case 7: case 8: sx0 = info.width-y-rows; sx1 = info.width-y; sy0 = 0; sy1 = info.height; break;
    default: break;
  }
  std::vector<uint16_t> row(size_t(sx1-sx0)*4);
  for (uint32_t sy=sy0;sy<sy1;++sy) {
    const uint64_t offset = (uint64_t(sy)*info.width+sx0)*8;
    const size_t bytes = row.size()*2;
    require(pread(fileno(file),row.data(),bytes,off_t(offset))==ssize_t(bytes), "Cannot read temporary pixel storage", PE_IO_ERROR);
    for (uint32_t sx=sx0;sx<sx1;++sx) {
      uint32_t dx=sx,dy=sy;
      switch(metadata.orientation) {
        case 2:dx=info.width-1-sx;break;
        case 3:dx=info.width-1-sx;dy=info.height-1-sy;break;
        case 4:dy=info.height-1-sy;break;
        case 5:dx=sy;dy=sx;break;
        case 6:dx=info.height-1-sy;dy=sx;break;
        case 7:dx=info.height-1-sy;dy=info.width-1-sx;break;
        case 8:dx=sy;dy=info.width-1-sx;break;
        default:break;
      }
      std::copy_n(row.data()+size_t(sx-sx0)*4,4,out->pixels.data()+(size_t(dy-y)*width+dx)*4);
    }
  }
  return out;
}
}
