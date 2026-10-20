#include "pe/pe.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using Image = std::unique_ptr<pe_image, decltype(&pe_image_free)>;
void check(pe_status status) { if (status != PE_OK) throw std::runtime_error(pe_last_error()); }
}
int main(int argc, char** argv) {
  try {
    if (argc < 3) {
      std::cout << "Photo Enhancement ABI " << pe_abi_version() << " [" << pe_capabilities() << "]\n"
          "Usage: photo_cli INPUT OUTPUT [--exposure EV] [--lut FILE] [--strength 0..1]\n"
          "                 [--border 0..0.25] [--bottom 0..0.4] [--font FILE]\n"
          "                 [--caption TEXT] [--max-edge PX] [--quality 1..100] [--no-metadata]\n"
          "Output: .jpg/.jpeg, .png (16-bit) or .heic/.heif. Existing files are NEVER overwritten.\n";
      return argc == 1 ? 0 : 2;
    }
    pe_decode_options options; pe_default_decode_options(&options); options.max_edge = 0;
    pe_params params; pe_default_params(&params);
    pe_border border; pe_default_border(&border);
    std::string lut_path; double strength = 1; uint32_t quality = 95; bool metadata = true;
    for (int i = 3; i < argc; ++i) {
      std::string key = argv[i];
      if (key == "--no-metadata") { metadata = false; continue; }
      if (++i == argc) throw std::runtime_error("Missing option value: " + key);
      std::string value = argv[i];
      if (key == "--exposure") params.exposure = std::stod(value);
      else if (key == "--lut") lut_path = value;
      else if (key == "--strength") strength = std::stod(value);
      else if (key == "--border") border.width = std::stod(value);
      else if (key == "--bottom") border.bottom = std::stod(value);
      else if (key == "--font") { if (value.size() >= sizeof(border.font_path)) throw std::runtime_error("Font path too long"); value.copy(border.font_path, value.size()); }
      else if (key == "--caption") { if (value.size() >= sizeof(border.text)) throw std::runtime_error("Caption too long"); value.copy(border.text, value.size()); }
      else if (key == "--max-edge") options.max_edge = uint32_t(std::stoul(value));
      else if (key == "--quality") quality = uint32_t(std::stoul(value));
      else throw std::runtime_error("Unknown option: " + key);
    }
    auto start = std::chrono::steady_clock::now();
    pe_image* handle = nullptr; check(pe_decode(argv[1], &options, &handle)); Image img(handle, pe_image_free);
    check(pe_apply_params(img.get(), &params, &handle)); img.reset(handle);
    if (!lut_path.empty()) {
      pe_lut* raw = nullptr; check(pe_lut_load(lut_path.c_str(), &raw));
      std::unique_ptr<pe_lut, decltype(&pe_lut_free)> lut(raw, pe_lut_free);
      check(pe_apply_lut(img.get(), lut.get(), strength, &handle)); img.reset(handle);
    }
    check(pe_add_border(img.get(), &border, &handle)); img.reset(handle);
    std::string output = argv[2], extension = output.substr(output.find_last_of('.') + 1);
    pe_format format;
    if (extension == "png") format = PE_PNG;
    else if (extension == "jpg" || extension == "jpeg") format = PE_JPEG;
    else if (extension == "heic" || extension == "heif") format = PE_HEIF;
    else throw std::runtime_error("Output extension must be jpg, jpeg, png, heic or heif");
    check(pe_encode(img.get(), output.c_str(), format, quality, metadata));
    pe_image_info info; check(pe_image_get_info(img.get(), &info));
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << info.width << "x" << info.height << " -> " << output << " (" << ms << " ms)\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
}
