#include "internal.hpp"
#include <sstream>

namespace pe {
std::unique_ptr<pe_lut> load_lut(const std::string& path) {
  std::ifstream file(path);
  require(bool(file), "Cannot open LUT: " + path, PE_IO_ERROR);
  require(std::filesystem::file_size(path) <= 16 * 1024 * 1024, "LUT file is too large", PE_LIMIT_EXCEEDED);
  auto lut = std::make_unique<pe_lut>();
  std::string line;
  bool have_size = false, have_low = false, have_high = false;
  while (std::getline(file, line)) {
    line = line.substr(0, line.find('#'));
    std::istringstream stream(line);
    std::string key;
    if (!(stream >> key)) continue;
    if (key == "TITLE") continue;
    if (key == "LUT_3D_SIZE") {
      require(!have_size && lut->values.empty() && bool(stream >> lut->size) && lut->size >= 2 && lut->size <= 65,
          "LUT_3D_SIZE must appear once and be in [2, 65]");
      have_size = true;
    } else if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX") {
      auto& v = key == "DOMAIN_MIN" ? lut->low : lut->high;
      bool& seen = key == "DOMAIN_MIN" ? have_low : have_high;
      require(!seen && lut->values.empty() && bool(stream >> v[0] >> v[1] >> v[2]), "Invalid LUT domain");
      seen = true;
    } else {
      require(have_size && lut->values.size() < size_t(lut->size) * lut->size * lut->size, "Unexpected LUT data");
      std::array<double, 3> v;
      std::istringstream values(line);
      require(bool(values >> v[0] >> v[1] >> v[2]), "Invalid LUT row or unsupported directive: " + key);
      std::string extra;
      require(!(values >> extra), "Unexpected trailing LUT values");
      for (double x : v) require(std::isfinite(x) && x >= 0 && x <= 1, "MVP LUT values must be in [0, 1]");
      lut->values.push_back(v);
    }
  }
  require(have_size && lut->values.size() == size_t(lut->size) * lut->size * lut->size, "Incomplete 3D LUT");
  for (int c = 0; c < 3; ++c)
    require(std::isfinite(lut->low[c]) && std::isfinite(lut->high[c]) && lut->high[c] > lut->low[c], "Invalid LUT domain range");
  return lut;
}

std::unique_ptr<pe_image> apply_lut(const pe_image& src, const pe_lut& lut, double strength) {
  require(std::isfinite(strength) && strength >= 0 && strength <= 1, "LUT strength must be in [0, 1]");
  auto out = std::make_unique<pe_image>(src);
  if (strength == 0) return out;
  for (size_t i = 0; i < src.pixels.size(); i += 4) {
    double input[3], fraction[3]; uint32_t base[3];
    for (int c = 0; c < 3; ++c) {
      input[c] = display(src.pixels[i + c] / 65535.0);
      double pos = clamp((input[c] - lut.low[c]) / (lut.high[c] - lut.low[c])) * (lut.size - 1);
      base[c] = std::min(uint32_t(pos), lut.size - 2); fraction[c] = pos - base[c];
    }
    double result[3]{};
    // .cube order: red changes fastest, blue changes slowest.
    for (uint32_t b = 0; b < 2; ++b) for (uint32_t g = 0; g < 2; ++g) for (uint32_t r = 0; r < 2; ++r) {
      const auto& value = lut.values[((base[2] + b) * lut.size + base[1] + g) * lut.size + base[0] + r];
      double weight = (r ? fraction[0] : 1 - fraction[0]) * (g ? fraction[1] : 1 - fraction[1]) * (b ? fraction[2] : 1 - fraction[2]);
      for (int c = 0; c < 3; ++c) result[c] += weight * value[c];
    }
    for (int c = 0; c < 3; ++c)
      out->pixels[i + c] = quantize(linear(input[c] * (1 - strength) + result[c] * strength));
  }
  return out;
}
}
