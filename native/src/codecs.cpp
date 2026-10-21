#include "internal.hpp"
#include "raw_support.hpp"
#include "heif_support.hpp"
#include <libraw/libraw.h>
#include <jpeglib.h>
#include <png.h>
#include <lcms2.h>
#include <csetjmp>
#include <climits>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#ifdef PE_HAVE_WEBP
#include <webp/decode.h>
#include <webp/demux.h>
#endif
#ifdef PE_HAVE_HEIF
#include <libheif/heif.h>
#include <libheif/heif_properties.h>
#endif

namespace pe {
namespace {
using Bytes = std::vector<uint8_t>;
Bytes read_file(const std::string& path, uint64_t limit) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  require(bool(stream), "Cannot open image: " + path, PE_IO_ERROR);
  auto size = stream.tellg();
  require(size > 0 && uint64_t(size) <= limit, "Input is empty or exceeds file size limit", PE_LIMIT_EXCEEDED);
  Bytes data(static_cast<size_t>(size));
  stream.seekg(0); stream.read(reinterpret_cast<char*>(data.data()), size);
  require(bool(stream), "Cannot read image", PE_IO_ERROR);
  return data;
}
bool heif_file(const std::string& path, uint64_t size) {
  std::array<uint8_t, 16> header{};
  std::ifstream file(path, std::ios::binary);
  file.read(reinterpret_cast<char*>(header.data()), header.size());
  if (file.gcount() < 16 || std::memcmp(header.data() + 4, "ftyp", 4) != 0) return false;
  const uint32_t length = uint32_t(header[0]) << 24 | uint32_t(header[1]) << 16 |
      uint32_t(header[2]) << 8 | header[3];
  require(length >= 16 && length <= size && length <= 4096,
      "Invalid ISO BMFF file type box", PE_DECODE_ERROR);
  Bytes brands(length);
  file.clear(); file.seekg(0);
  file.read(reinterpret_cast<char*>(brands.data()), length);
  require(bool(file), "Cannot read image file type box", PE_IO_ERROR);
  bool heif = false, cr3 = false;
  for (uint32_t offset = 8; offset + 4 <= length; offset += 4) {
    if (offset == 12) continue;
    const auto* brand = brands.data() + offset;
    cr3 = cr3 || std::memcmp(brand, "crx ", 4) == 0;
    for (const char* name : {"heic", "heix", "hevc", "hevx", "mif1", "msf1", "avif", "avis"})
      heif = heif || std::memcmp(brand, name, 4) == 0;
  }
  return heif && !cr3;
}
struct JpegError { jpeg_error_mgr manager{}; std::jmp_buf jump; char message[JMSG_LENGTH_MAX]{}; };
void jpeg_error(j_common_ptr common) {
  auto* error = reinterpret_cast<JpegError*>(common->err);
  error->manager.format_message(common, error->message);
  std::longjmp(error->jump, 1);
}
struct JpegRead {
  jpeg_decompress_struct codec{}; JpegError error; bool created = false;
  FILE* file = nullptr;
  std::unique_ptr<pe_image> chunk;
  Bytes row; std::vector<uint16_t> pixels;
  Metadata meta; Bytes icc; std::array<Bytes, 256> chunks;
  ~JpegRead() { if (created) jpeg_destroy_decompress(&codec); if(file) std::fclose(file); }
};
void jpeg_header(JpegRead* state, const std::string& path) {
  jpeg_create_decompress(&state->codec); state->created = true;
  state->file = std::fopen(path.c_str(), "rb");
  require(state->file, "Cannot open JPEG", PE_IO_ERROR);
  jpeg_stdio_src(&state->codec, state->file);
  jpeg_save_markers(&state->codec, JPEG_APP0 + 1, 65535);
  jpeg_save_markers(&state->codec, JPEG_APP0 + 2, 65535);
  jpeg_read_header(&state->codec, TRUE);
  checked_pixels(state->codec.image_width, state->codec.image_height);
  unsigned chunk_count = 0;
  for (auto* marker = state->codec.marker_list; marker; marker = marker->next) {
    if (marker->marker == JPEG_APP0 + 1 && marker->data_length >= 6 && std::memcmp(marker->data, "Exif\0\0", 6) == 0)
      state->meta = read_exif(marker->data, marker->data_length);
    if (marker->marker == JPEG_APP0 + 2 && marker->data_length >= 14 && std::memcmp(marker->data, "ICC_PROFILE\0", 12) == 0) {
      unsigned index = marker->data[12], count = marker->data[13];
      require(index && count && index <= count && (!chunk_count || chunk_count == count) && state->chunks[index].empty(), "Malformed JPEG ICC chunks", PE_DECODE_ERROR);
      chunk_count = count;
      state->chunks[index].assign(marker->data + 14, marker->data + marker->data_length);
    }
  }
  for (unsigned n = 1; n <= chunk_count; ++n) {
    require(!state->chunks[n].empty(), "Incomplete JPEG ICC profile", PE_DECODE_ERROR);
    state->icc.insert(state->icc.end(), state->chunks[n].begin(), state->chunks[n].end());
  }
  require(state->codec.jpeg_color_space != JCS_CMYK && state->codec.jpeg_color_space != JCS_YCCK,
      "CMYK JPEG is not supported; convert it to RGB first", PE_UNSUPPORTED);
  state->meta.info.width = state->codec.image_width; state->meta.info.height = state->codec.image_height;
}
void jpeg_decoder_budget(JpegRead* state, uint64_t budget, uint64_t working) {
  // Progressive JPEG retains full-size coefficient arrays even with IDCT scaling.
  const uint64_t coefficients = state->codec.progressive_mode
      ? uint64_t(state->codec.image_width)*state->codec.image_height*12 : 0;
  require_memory(working + coefficients + 8*1024*1024, budget,
      state->codec.progressive_mode ? "渐进式 JPEG 解码（含全尺寸系数）" : "JPEG 解码");
  state->codec.mem->max_memory_to_use = long(std::min<uint64_t>(budget, LONG_MAX));
}
std::unique_ptr<pe_image> jpeg_decode(const std::string& path, const pe_decode_options& options) {
  auto state = std::make_unique<JpegRead>();
  state->codec.err = jpeg_std_error(&state->error.manager);
  state->error.manager.error_exit = jpeg_error;
  if (setjmp(state->error.jump)) throw Error(PE_DECODE_ERROR, state->error.message);
  jpeg_header(state.get(), path);
  if (options.max_edge) {
    const unsigned edge = std::max(state->codec.image_width, state->codec.image_height);
    // Choose scaled IDCT near the preview size, not a full image then resize.
    if (edge > options.max_edge) {
      state->codec.scale_num = std::max(1U, unsigned(uint64_t(options.max_edge)*8/edge));
      state->codec.scale_denom = 8;
    }
  }
  state->codec.out_color_space = JCS_RGB;
  jpeg_calc_output_dimensions(&state->codec);
  checked_pixels(state->codec.output_width, state->codec.output_height, options.max_pixels);
  jpeg_decoder_budget(state.get(), memory_budget(options.max_edge != 0),
      uint64_t(state->codec.output_width)*state->codec.output_height*28);
  jpeg_start_decompress(&state->codec);
  uint32_t w = state->codec.output_width, h = state->codec.output_height;
  state->pixels.resize(checked_pixels(w, h, options.max_pixels) * 4);
  state->row.resize(size_t(w) * 3);
  while (state->codec.output_scanline < h) {
    uint32_t y = state->codec.output_scanline;
    JSAMPROW row = state->row.data(); jpeg_read_scanlines(&state->codec, &row, 1);
    for (uint32_t x = 0; x < w; ++x) {
      size_t p = (size_t(y) * w + x) * 4;
      for (int c = 0; c < 3; ++c) state->pixels[p + c] = uint16_t(state->row[x * 3 + c]) * 257;
      state->pixels[p + 3] = 65535;
    }
  }
  jpeg_finish_decompress(&state->codec);
  auto out = image(w, h);
  from_display(*out, state->pixels.data(), state->icc);
  state->meta.info.width = w; state->meta.info.height = h; out->info = state->meta.info;
  orient(*out, state->meta.orientation);
  return out;
}

struct PngState {
  png_structp codec = nullptr; png_infop info = nullptr;
  const Bytes* input = nullptr; size_t position = 0;
  Bytes data, icc, exif; std::vector<png_bytep> rows;
  bool writing = false; FILE* file = nullptr;
  ~PngState() {
    if (codec) {
      if (writing) png_destroy_write_struct(&codec, &info);
      else png_destroy_read_struct(&codec, &info, nullptr);
    }
  }
};
void png_input(png_structp png, png_bytep buffer, png_size_t count) {
  auto* state = static_cast<PngState*>(png_get_io_ptr(png));
  if (count > state->input->size() - state->position) png_error(png, "Truncated PNG");
  std::memcpy(buffer, state->input->data() + state->position, count); state->position += count;
}
std::unique_ptr<pe_image> png_decode(const Bytes& bytes, const pe_decode_options& options) {
  auto state = std::make_unique<PngState>(); state->input = &bytes;
  state->codec = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  require(state->codec, "Cannot allocate PNG decoder", PE_OUT_OF_MEMORY);
  state->info = png_create_info_struct(state->codec);
  require(state->info, "Cannot allocate PNG metadata", PE_OUT_OF_MEMORY);
  if (setjmp(png_jmpbuf(state->codec))) throw Error(PE_DECODE_ERROR, "Invalid or truncated PNG");
  png_set_read_fn(state->codec, state.get(), png_input);
  png_set_user_limits(state->codec, 32768, 32768);
  png_set_chunk_malloc_max(state->codec, 16 * 1024 * 1024);
  png_set_chunk_cache_max(state->codec, 128);
  png_read_info(state->codec, state->info);
  uint32_t w = png_get_image_width(state->codec, state->info), h = png_get_image_height(state->codec, state->info);
  checked_pixels(w, h, options.max_pixels);
  require_memory(uint64_t(w)*h*32 + bytes.size(), memory_budget(options.max_edge != 0), "PNG 解码");
  int depth = png_get_bit_depth(state->codec, state->info), color = png_get_color_type(state->codec, state->info);
  png_charp profile_name; int compression; png_bytep profile; png_uint_32 profile_size;
  if (png_get_iCCP(state->codec, state->info, &profile_name, &compression, &profile, &profile_size))
    state->icc.assign(profile, profile + profile_size);
#ifdef PNG_eXIf_SUPPORTED
  png_bytep exif; png_uint_32 exif_size;
  if (png_get_eXIf_1(state->codec, state->info, &exif_size, &exif)) state->exif.assign(exif, exif + exif_size);
#endif
  if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(state->codec);
  if (color == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(state->codec);
  bool transparency = png_get_valid(state->codec, state->info, PNG_INFO_tRNS);
  if (transparency) png_set_tRNS_to_alpha(state->codec);
  if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(state->codec);
  if (!(color & PNG_COLOR_MASK_ALPHA) && !transparency) png_set_add_alpha(state->codec, depth == 16 ? 65535 : 255, PNG_FILLER_AFTER);
  const uint16_t endian = 1;
  if (depth == 16 && *reinterpret_cast<const uint8_t*>(&endian)) png_set_swap(state->codec);
  png_set_interlace_handling(state->codec);
  png_read_update_info(state->codec, state->info);
  size_t stride = png_get_rowbytes(state->codec, state->info);
  state->data.resize(stride * h); state->rows.resize(h);
  for (uint32_t y = 0; y < h; ++y) state->rows[y] = state->data.data() + size_t(y) * stride;
  png_read_image(state->codec, state->rows.data()); png_read_end(state->codec, state->info);
  auto out = image(w, h);
  std::vector<uint16_t> pixels(out->pixels.size());
  if (depth == 16) std::memcpy(pixels.data(), state->data.data(), pixels.size() * 2);
  else for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = uint16_t(state->data[i]) * 257;
  from_display(*out, pixels.data(), state->icc);
  auto meta = read_exif(state->exif.data(), state->exif.size());
  meta.info.width = w; meta.info.height = h; out->info = meta.info;
  orient(*out, meta.orientation);
  return out;
}

std::unique_ptr<pe_image> raw_decode(const std::string& path, const pe_decode_options& options) {
  LibRaw raw;
  constexpr uint64_t MiB = 1024ULL * 1024;
  const uint64_t budget = memory_budget();
  // LibRaw's own limit covers RAW allocation paths, not the whole application.
  // Retain its default cap if it is stricter than our current device budget.
  raw.imgdata.rawparams.max_raw_memory_mb = unsigned(std::min<uint64_t>(
      raw.imgdata.rawparams.max_raw_memory_mb, budget / MiB));
  int result = raw.open_file(path.c_str());
  require(result == LIBRAW_SUCCESS, std::string("LibRaw: ") + libraw_strerror(result), PE_UNSUPPORTED);
  checked_pixels(raw.imgdata.sizes.raw_width, raw.imgdata.sizes.raw_height, options.max_pixels);
  checked_pixels(raw.imgdata.sizes.width, raw.imgdata.sizes.height, options.max_pixels);
  raw.imgdata.params.output_bps = 16;
  raw.imgdata.params.output_color = 1;
  raw.imgdata.params.gamm[0] = 1; raw.imgdata.params.gamm[1] = 1;
  raw.imgdata.params.no_auto_bright = 1;
  raw.imgdata.params.use_camera_wb = 1;
  raw.imgdata.params.use_auto_wb = 0;
  raw.imgdata.params.user_qual = 3;
  raw.imgdata.params.half_size = options.max_edge &&
      std::max(raw.imgdata.sizes.width, raw.imgdata.sizes.height) > options.max_edge * 2ULL;

  libraw_decoder_info_t decoder{};
  const bool known_decoder = raw.get_decoder_info(&decoder) == LIBRAW_SUCCESS &&
      !(decoder.decoder_flags & (LIBRAW_DECODER_OWNALLOC | LIBRAW_DECODER_3CHANNEL |
          LIBRAW_DECODER_UNSUPPORTED_FORMAT | LIBRAW_DECODER_NOTSET));
  const bool half_preview = raw.imgdata.params.half_size && known_decoder &&
      (raw.imgdata.idata.filters > 1000 || raw.imgdata.idata.filters == LIBRAW_XTRANS) &&
      !raw.is_fuji_rotated() && !raw.is_floating_point() && !raw.is_sraw() &&
      std::abs(raw.imgdata.sizes.pixel_aspect - 1) < .005;
  const uint32_t sensor_width = std::max(uint32_t(raw.imgdata.sizes.raw_width),
      uint32_t(raw.imgdata.sizes.width) + raw.imgdata.sizes.left_margin);
  const uint32_t sensor_height = std::max(uint32_t(raw.imgdata.sizes.raw_height),
      uint32_t(raw.imgdata.sizes.height) + raw.imgdata.sizes.top_margin);
  checked_pixels(sensor_width, sensor_height, options.max_pixels);
  auto check_budget = [&](bool reduced, uint32_t width, uint32_t height) {
    checked_pixels(width, height, options.max_pixels);
    const uint64_t estimate = reduced
        ? raw_preview_memory(sensor_width, sensor_height, width, height).peak()
        : uint64_t(sensor_width) * sensor_height * 64;
    require_memory(estimate, std::min(budget, memory_budget()), reduced
        ? "RAW 半尺寸预览（含完整传感器解包）"
        : "RAW 解码（此路径按完整图像缓冲估算）");
  };
  check_budget(half_preview, raw.imgdata.sizes.width, raw.imgdata.sizes.height);
  auto check_raw = [&](int status, const char* stage) {
    require(status == LIBRAW_SUCCESS, std::string(stage) + libraw_strerror(status),
        status == LIBRAW_TOO_BIG ? PE_LIMIT_EXCEEDED :
        status == LIBRAW_UNSUFFICIENT_MEMORY ? PE_OUT_OF_MEMORY : PE_DECODE_ERROR);
  };
  result = raw.unpack();
  check_raw(result, "LibRaw unpack: ");
  // raw2image_start() only shrinks single-plane CFA buffers. Confirm the actual
  // unpacked representation before allowing the smaller processing estimate.
  const auto& data = raw.imgdata.rawdata;
  const bool actual_half = half_preview && data.iparams.filters && data.raw_image &&
      !data.color3_image && !data.color4_image && !data.float3_image && !data.float4_image;
  check_budget(actual_half, data.sizes.width, data.sizes.height);
  result = raw.dcraw_process();
  check_raw(result, "LibRaw process: ");
  int width = 0, height = 0, colors = 0, bits = 0;
  raw.get_mem_image_format(&width, &height, &colors, &bits);
  require(width > 0 && height > 0 && bits == 16 && (colors == 3 || colors == 4),
      "LibRaw did not produce RGB16 output", PE_DECODE_ERROR);
  const uint64_t pixels = checked_pixels(uint32_t(width), uint32_t(height), options.max_pixels);
  require_memory(uint64_t(sensor_width) * sensor_height * 4 + pixels * 16 + 64 * MiB,
      std::min(budget, memory_budget()), "RAW 输出像素转换");
  auto out = image(uint32_t(width), uint32_t(height));
  // copy_mem_image respects EXIF rotation and pixel-aspect processing. Give it
  // RGBA row stride, then expand packed RGB in place from RIGHT to LEFT.
  // This removes dcraw_make_mem_image's additional full RGB16 allocation.
  check_raw(raw.copy_mem_image(out->pixels.data(), width * 8, 0), "LibRaw output: ");
  for (int y = 0; y < height; ++y) {
    auto* row = out->pixels.data() + size_t(y) * size_t(width) * 4;
    expand_raw_row(row, uint32_t(width), uint32_t(colors));
  }
  auto& info = out->info; info.is_raw = 1;
  std::snprintf(info.make, sizeof(info.make), "%s", raw.imgdata.idata.make);
  std::snprintf(info.model, sizeof(info.model), "%s", raw.imgdata.idata.model);
  info.iso = uint32_t(std::max(0.f, raw.imgdata.other.iso_speed));
  info.aperture = raw.imgdata.other.aperture; info.shutter = raw.imgdata.other.shutter;
  info.focal_length = raw.imgdata.other.focal_len;
  if (raw.imgdata.other.timestamp > 0) {
    // LibRaw parses timezone-less camera timestamps with mktime(). Reverse
    // that conversion locally; never label the camera wall time as UTC.
    std::tm captured{};
    if (localtime_r(&raw.imgdata.other.timestamp, &captured)) {
      std::strftime(info.captured_at, sizeof(info.captured_at), "%Y:%m:%d %H:%M:%S", &captured);
    }
  }
  return out;
}

#ifdef PE_HAVE_WEBP
std::unique_ptr<pe_image> webp_decode(const Bytes& bytes, const pe_decode_options& options) {
  WebPBitstreamFeatures features{};
  require(WebPGetFeatures(bytes.data(), bytes.size(), &features) == VP8_STATUS_OK, "Invalid WebP", PE_DECODE_ERROR);
  require(!features.has_animation, "Animated WebP is not supported", PE_UNSUPPORTED);
  checked_pixels(uint32_t(features.width), uint32_t(features.height), options.max_pixels);
  require_memory(uint64_t(features.width)*features.height*32 + bytes.size(), memory_budget(options.max_edge != 0), "WebP 解码");
  Bytes rgba(size_t(features.width) * features.height * 4);
  require(WebPDecodeRGBAInto(bytes.data(), bytes.size(), rgba.data(), rgba.size(), features.width * 4), "Cannot decode WebP", PE_DECODE_ERROR);
  WebPData data{bytes.data(), bytes.size()};
  std::unique_ptr<WebPDemuxer, decltype(&WebPDemuxDelete)> demux(WebPDemux(&data), WebPDemuxDelete);
  Bytes icc; Metadata meta; WebPChunkIterator iter;
  if (demux && WebPDemuxGetChunk(demux.get(), "ICCP", 1, &iter)) {
    icc.assign(iter.chunk.bytes, iter.chunk.bytes + iter.chunk.size); WebPDemuxReleaseChunkIterator(&iter);
  }
  if (demux && WebPDemuxGetChunk(demux.get(), "EXIF", 1, &iter)) {
    meta = read_exif(iter.chunk.bytes, iter.chunk.size); WebPDemuxReleaseChunkIterator(&iter);
  }
  auto out = image(uint32_t(features.width), uint32_t(features.height));
  std::vector<uint16_t> pixels(rgba.size());
  for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = uint16_t(rgba[i]) * 257;
  from_display(*out, pixels.data(), icc);
  meta.info.width = out->info.width; meta.info.height = out->info.height; out->info = meta.info;
  orient(*out, meta.orientation);
  return out;
}
#endif

#ifdef PE_HAVE_HEIF
void heif_check(heif_error e) { require(e.code == heif_error_Ok, e.message ? e.message : "HEIF codec error", PE_DECODE_ERROR); }
Bytes nclx_profile(const heif_color_profile_nclx& nclx) {
  cmsCIExyY white{.3127, .3290, 1};
  cmsCIExyYTRIPLE primaries{{.64,.33,1},{.30,.60,1},{.15,.06,1}};
  if (nclx.color_primaries == 12) primaries = {{.68,.32,1},{.265,.69,1},{.15,.06,1}};
  else if (nclx.color_primaries == 9) primaries = {{.708,.292,1},{.170,.797,1},{.131,.046,1}};
  else require(nclx.color_primaries == 1 || nclx.color_primaries == 2,
      "Unsupported HEIF color primaries without an ICC profile", PE_UNSUPPORTED);
  int transfer = nclx.transfer_characteristics;
  require(transfer == 13 || transfer == 1 || transfer == 6 || transfer == 14 || transfer == 15 || transfer == 8 || transfer == 2,
      "Unsupported HEIF transfer function (HDR tone mapping is not implemented)", PE_UNSUPPORTED);
  std::array<cmsUInt16Number,4096> table{};
  for (size_t i = 0; i < table.size(); ++i) {
    double v = double(i) / (table.size() - 1);
    table[i] = quantize(transfer == 8 ? v : transfer == 13 || transfer == 2 ? linear(v) :
        v < .081 ? v / 4.5 : std::pow((v + .099) / 1.099, 1 / .45));
  }
  cmsToneCurve* curve = cmsBuildTabulatedToneCurve16(nullptr, uint32_t(table.size()), table.data());
  require(curve, "Cannot create HEIF tone curve", PE_OUT_OF_MEMORY);
  cmsToneCurve* curves[]{curve,curve,curve};
  cmsHPROFILE profile = cmsCreateRGBProfile(&white,&primaries,curves); cmsFreeToneCurve(curve);
  require(profile, "Cannot create HEIF color profile", PE_DECODE_ERROR);
  cmsUInt32Number length=0; cmsSaveProfileToMem(profile,nullptr,&length); Bytes bytes(length);
  cmsSaveProfileToMem(profile,bytes.data(),&length); cmsCloseProfile(profile); return bytes;
}
std::unique_ptr<pe_image> heif_decode(const Bytes& bytes, const pe_decode_options& options) {
  std::unique_ptr<heif_context, decltype(&heif_context_free)> context(heif_context_alloc(), heif_context_free);
  require(bool(context), "Cannot allocate HEIF context", PE_OUT_OF_MEMORY);
  heif_context_set_maximum_image_size_limit(context.get(), 32768);
  heif_check(heif_context_read_from_memory_without_copy(context.get(), bytes.data(), bytes.size(), nullptr));
  heif_image_handle* raw_handle = nullptr;
  heif_check(heif_context_get_primary_image_handle(context.get(), &raw_handle));
  std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)> handle(raw_handle, heif_image_handle_release);
  int w = heif_image_handle_get_width(handle.get()), h = heif_image_handle_get_height(handle.get());
  checked_pixels(uint32_t(w), uint32_t(h), options.max_pixels);
  require_memory(uint64_t(w)*h*40 + bytes.size(), memory_budget(options.max_edge != 0), "HEIF 解码");
  Bytes icc;
  auto icc_size = heif_image_handle_get_raw_color_profile_size(handle.get());
  require(icc_size <= 16 * 1024 * 1024, "HEIF ICC profile is too large", PE_LIMIT_EXCEEDED);
  icc.resize(icc_size);
  if (!icc.empty()) heif_check(heif_image_handle_get_raw_color_profile(handle.get(), icc.data()));
  // HDR needs a defined tone mapper. Never silently treat PQ/HLG as SDR sRGB.
  heif_color_profile_nclx* nclx = nullptr;
  if (heif_image_handle_get_nclx_color_profile(handle.get(), &nclx).code == heif_error_Ok && nclx) {
    std::unique_ptr<heif_color_profile_nclx, decltype(&heif_nclx_color_profile_free)> owner(nclx, heif_nclx_color_profile_free);
    require(nclx->transfer_characteristics != 16 && nclx->transfer_characteristics != 18,
        "HDR HEIF (PQ/HLG) requires tone mapping and is not supported in MVP", PE_UNSUPPORTED);
    if (icc.empty()) icc = nclx_profile(*nclx);
  }
  heif_image* raw_image = nullptr;
  heif_check(heif_decode_image(handle.get(), &raw_image, heif_colorspace_RGB, heif_chroma_interleaved_RRGGBBAA_LE, nullptr));
  std::unique_ptr<heif_image, decltype(&heif_image_release)> decoded(raw_image, heif_image_release);
  int stride = 0;
  const uint8_t* plane = heif_image_get_plane_readonly(decoded.get(), heif_channel_interleaved, &stride);
  require(plane, "HEIF has no RGB plane", PE_DECODE_ERROR);
  int depth = heif_image_get_bits_per_pixel_range(decoded.get(), heif_channel_interleaved);
  require(depth > 0 && depth <= 16, "Invalid HEIF bit depth", PE_DECODE_ERROR);
  auto out = image(uint32_t(w), uint32_t(h)); std::vector<uint16_t> pixels(out->pixels.size());
  double maximum = double((1U << depth) - 1);
  for (int y = 0; y < h; ++y) for (int x = 0; x < w * 4; ++x) {
    const auto* p = plane + size_t(y) * stride + x * 2;
    pixels[(size_t(y) * w * 4) + x] = quantize((p[0] | uint16_t(p[1]) << 8) / maximum);
  }
  from_display(*out, pixels.data(), icc);
  heif_item_id id;
  if (heif_image_handle_get_list_of_metadata_block_IDs(handle.get(), "Exif", &id, 1) == 1) {
    auto exif_size = heif_image_handle_get_metadata_size(handle.get(), id);
    require(exif_size <= 16 * 1024 * 1024, "HEIF metadata is too large", PE_LIMIT_EXCEEDED);
    Bytes exif(exif_size);
    heif_check(heif_image_handle_get_metadata(handle.get(), id, exif.data()));
    if (exif.size() >= 4) {
      uint32_t offset = uint32_t(exif[0]) << 24 | uint32_t(exif[1]) << 16 | uint32_t(exif[2]) << 8 | exif[3];
      if (uint64_t(offset) + 4 <= exif.size()) {
        auto meta = read_exif(exif.data() + 4 + offset, exif.size() - 4 - offset);
        meta.info.width = uint32_t(w); meta.info.height = uint32_t(h); out->info = meta.info;
      }
    }
  }
  // libheif has already applied irot/imir; never apply EXIF orientation again.
  return out;
}
#endif

struct Destination {
  std::string path; FILE* file = nullptr; bool committed = false;
  explicit Destination(std::string p) : path(std::move(p)) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    require(fd >= 0, "Destination already exists or cannot be created: " + path, PE_IO_ERROR);
    file = fdopen(fd, "wb");
    if (!file) { ::close(fd); ::unlink(path.c_str()); throw Error(PE_IO_ERROR, "Cannot open output stream"); }
  }
  ~Destination() { if (file) std::fclose(file); if (!committed) ::unlink(path.c_str()); }
  void finish() {
    bool ok = std::fflush(file) == 0 && ::fsync(fileno(file)) == 0;
    int status = std::fclose(file); file = nullptr;
    require(ok && status == 0, "Cannot flush encoded image", PE_IO_ERROR); committed = true;
  }
};
struct JpegWrite {
  jpeg_compress_struct codec{}; JpegError error; bool created = false;
  Bytes row, exif;
  ~JpegWrite() { if (created) jpeg_destroy_compress(&codec); }
};
struct JpegRows final : RowWriter {
  std::unique_ptr<JpegWrite> state = std::make_unique<JpegWrite>();
  explicit JpegRows(FILE* file, const pe_image_info& info, uint32_t quality, bool metadata) {
    auto& c = state->codec;
    c.err = jpeg_std_error(&state->error.manager); state->error.manager.error_exit = jpeg_error;
    if(setjmp(state->error.jump)) throw Error(PE_IO_ERROR,state->error.message);
    jpeg_create_compress(&c); state->created=true; jpeg_stdio_dest(&c,file);
    c.image_width=info.width;c.image_height=info.height;c.input_components=3;c.in_color_space=JCS_RGB;
    jpeg_set_defaults(&c);jpeg_set_quality(&c,int(quality),TRUE);
    for(int n=0;n<3;++n){c.comp_info[n].h_samp_factor=1;c.comp_info[n].v_samp_factor=1;}
    jpeg_start_compress(&c,TRUE);
    if(metadata){state->exif=write_exif(info);state->exif.insert(state->exif.begin(),{'E','x','i','f',0,0});
      jpeg_write_marker(&c,JPEG_APP0+1,state->exif.data(),unsigned(state->exif.size()));}
    state->row.resize(size_t(info.width)*3);
  }
  void rows(const pe_image& img,uint32_t begin,uint32_t count) override {
    require(img.info.width==state->codec.image_width && begin<=img.info.height && count<=img.info.height-begin,"Invalid JPEG output rows");
    if(setjmp(state->error.jump)) throw Error(PE_IO_ERROR,state->error.message);
    for(uint32_t y=begin;y<begin+count;++y){
      for(uint32_t x=0;x<img.info.width;++x){
        const size_t p=(size_t(y)*img.info.width+x)*4;const double alpha=img.pixels[p+3]/65535.0;
        for(int c=0;c<3;++c)state->row[size_t(x)*3+c]=uint8_t(std::lround(clamp(display(img.pixels[p+c]/65535.0*alpha+1-alpha))*255));
      }
      JSAMPROW row=state->row.data();jpeg_write_scanlines(&state->codec,&row,1);
    }
  }
  void finish() override {if(setjmp(state->error.jump))throw Error(PE_IO_ERROR,state->error.message);jpeg_finish_compress(&state->codec);}
};
struct PngRows final : RowWriter {
  std::unique_ptr<PngState> state = std::make_unique<PngState>();
  uint32_t width;
  explicit PngRows(FILE* file,const pe_image_info& info,bool metadata):width(info.width){
    state->writing=true;state->codec=png_create_write_struct(PNG_LIBPNG_VER_STRING,nullptr,nullptr,nullptr);
    require(state->codec,"Cannot allocate PNG encoder",PE_OUT_OF_MEMORY);
    state->info=png_create_info_struct(state->codec);require(state->info,"Cannot allocate PNG metadata",PE_OUT_OF_MEMORY);
    if(setjmp(png_jmpbuf(state->codec)))throw Error(PE_IO_ERROR,"PNG encoding failed");
    png_init_io(state->codec,file);png_set_IHDR(state->codec,state->info,info.width,info.height,16,PNG_COLOR_TYPE_RGBA,PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
    png_set_sRGB(state->codec,state->info,PNG_sRGB_INTENT_RELATIVE);
#ifdef PNG_eXIf_SUPPORTED
    if(metadata){state->exif=write_exif(info);png_set_eXIf_1(state->codec,state->info,png_uint_32(state->exif.size()),state->exif.data());}
#endif
    png_write_info(state->codec,state->info);state->data.resize(size_t(width)*8);
  }
  void rows(const pe_image& img,uint32_t begin,uint32_t count) override {
    require(img.info.width==width && begin<=img.info.height && count<=img.info.height-begin,"Invalid PNG output rows");
    if(setjmp(png_jmpbuf(state->codec)))throw Error(PE_IO_ERROR,"PNG encoding failed");
    for(uint32_t y=begin;y<begin+count;++y){
      for(size_t x=0;x<size_t(width)*4;++x){const auto pixel=img.pixels[size_t(y)*width*4+x];
        const uint16_t value=x%4==3?pixel:quantize(display(pixel/65535.0));
        state->data[x*2]=uint8_t(value>>8);state->data[x*2+1]=uint8_t(value);
      }
      png_write_row(state->codec,state->data.data());
    }
  }
  void finish() override {if(setjmp(png_jmpbuf(state->codec)))throw Error(PE_IO_ERROR,"PNG encoding failed");png_write_end(state->codec,state->info);}
};
#ifdef PE_HAVE_HEIF
heif_error heif_output(heif_context*, const void* data, size_t size, void* user) {
  if (std::fwrite(data, 1, size, static_cast<FILE*>(user)) != size)
    return {heif_error_Encoding_error, heif_suberror_Unspecified, "Cannot write HEIF output"};
  return {heif_error_Ok, heif_suberror_Unspecified, "Success"};
}
// Small images use one plane; large images encode uniform grid tiles from a
// bounded RGB band. Compressed tiles stay in the libheif context until finish().
struct HeifRows final : RowWriter {
  std::unique_ptr<heif_context, decltype(&heif_context_free)> context{nullptr, heif_context_free};
  std::unique_ptr<heif_encoder, decltype(&heif_encoder_release)> encoder{nullptr, heif_encoder_release};
  std::unique_ptr<heif_image, decltype(&heif_image_release)> bitmap{nullptr, heif_image_release};
  FILE* file;
  pe_image_info info;
  bool metadata, grid;
  std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)>
      handle{nullptr, heif_image_handle_release};
  uint32_t written = 0;
  int stride = 0;
  uint8_t* plane = nullptr;
  HeifRows(FILE* output, const pe_image_info& value, uint32_t quality, bool keep_metadata)
      : file(output), info(value), metadata(keep_metadata), grid(heif_grid(value.width,value.height)) {
    context.reset(heif_context_alloc());
    require(bool(context), "Cannot allocate HEIF context", PE_OUT_OF_MEMORY);
    heif_encoder* raw_encoder = nullptr;
    heif_check(heif_context_get_encoder_for_format(context.get(), heif_compression_HEVC, &raw_encoder));
    encoder.reset(raw_encoder);
    heif_check(heif_encoder_set_lossy_quality(encoder.get(), int(quality)));
    bitmap = make_bitmap(info.width, grid ? heif_tile_edge : info.height);
    plane = heif_image_get_plane(bitmap.get(), heif_channel_interleaved, &stride);
    require(plane && stride >= int(info.width * 3), "Invalid HEIF output plane", PE_OUT_OF_MEMORY);
    if (grid) {
      heif_image_handle* raw_handle = nullptr;
      heif_check(heif_context_add_grid_image(context.get(), info.width, info.height,
          (info.width + heif_tile_edge - 1) / heif_tile_edge,
          (info.height + heif_tile_edge - 1) / heif_tile_edge, nullptr, &raw_handle));
      handle.reset(raw_handle);
      heif_check(heif_context_set_primary_image(context.get(), handle.get()));
      // colr payload: nclx, BT.709 primaries, sRGB transfer, BT.601 matrix,
      // limited-range flag. Attach to the displayed grid as well as each tile.
      const uint8_t profile[]{'n','c','l','x',0,1,0,13,0,6,0};
      heif_check(heif_item_add_raw_property(context.get(), heif_image_handle_get_item_id(handle.get()),
          0x636f6c72, nullptr, profile, sizeof(profile), 0, nullptr));
    }
  }
  static std::unique_ptr<heif_image, decltype(&heif_image_release)>
      make_bitmap(uint32_t width, uint32_t height) {
    heif_image* raw_image = nullptr;
    heif_check(heif_image_create(int(width), int(height), heif_colorspace_RGB,
        heif_chroma_interleaved_RGB, &raw_image));
    std::unique_ptr<heif_image, decltype(&heif_image_release)> image(raw_image, heif_image_release);
    heif_check(heif_image_add_plane(image.get(), heif_channel_interleaved, int(width), int(height), 8));
    std::unique_ptr<heif_color_profile_nclx, decltype(&heif_nclx_color_profile_free)>
        nclx(heif_nclx_color_profile_alloc(), heif_nclx_color_profile_free);
    require(bool(nclx), "Cannot allocate HEIF color profile", PE_OUT_OF_MEMORY);
    heif_nclx_color_profile_set_color_primaries(nclx.get(), 1);
    heif_nclx_color_profile_set_transfer_characteristics(nclx.get(), 13);
    heif_nclx_color_profile_set_matrix_coefficients(nclx.get(), 6);
    heif_check(heif_image_set_nclx_color_profile(image.get(), nclx.get()));
    return image;
  }
  void flush_band(uint32_t valid_rows) {
    const uint32_t tile_y = (written - 1) / heif_tile_edge;
    for (uint32_t x = 0; x < info.width; x += heif_tile_edge) {
      auto tile = make_bitmap(heif_tile_edge, heif_tile_edge);
      int tile_stride = 0;
      auto* target = heif_image_get_plane(tile.get(), heif_channel_interleaved, &tile_stride);
      require(target && tile_stride >= int(heif_tile_edge * 3), "Invalid HEIF tile", PE_OUT_OF_MEMORY);
      const uint32_t columns = std::min(heif_tile_edge, info.width - x);
      for (uint32_t y = 0; y < heif_tile_edge; ++y) {
        const auto* source = plane + size_t(std::min(y, valid_rows - 1)) * stride + size_t(x) * 3;
        auto* row = target + size_t(y) * tile_stride;
        std::copy_n(source, size_t(columns) * 3, row);
        // Replicate edge pixels; the grid retains exact unpadded dimensions.
        for (uint32_t col = columns; col < heif_tile_edge; ++col)
          std::copy_n(source + size_t(columns - 1) * 3, 3, row + size_t(col) * 3);
      }
      heif_check(heif_context_add_image_tile(context.get(), handle.get(),
          x / heif_tile_edge, tile_y, tile.get(), encoder.get()));
    }
  }
  void rows(const pe_image& img, uint32_t begin, uint32_t count) override {
    require(img.info.width == info.width && begin <= img.info.height &&
        count <= img.info.height - begin && count <= info.height - written,
        "Invalid HEIF output rows");
    for (uint32_t y = 0; y < count; ++y) {
      auto* target = plane + size_t(grid ? written % heif_tile_edge : written) * stride;
      for (uint32_t x = 0; x < info.width; ++x) {
        const size_t p = (size_t(begin + y) * info.width + x) * 4;
        const double alpha = img.pixels[p + 3] / 65535.0;
        for (int c = 0; c < 3; ++c)
          target[size_t(x) * 3 + c] = uint8_t(std::lround(clamp(
              display(img.pixels[p + c] / 65535.0 * alpha + 1 - alpha)) * 255));
      }
      ++written;
      if (grid && (written % heif_tile_edge == 0 || written == info.height))
        flush_band(written % heif_tile_edge ? written % heif_tile_edge : heif_tile_edge);
    }
  }
  void finish() override {
    require(written == info.height, "Incomplete HEIF output rows");
    if (!grid) {
      heif_image_handle* raw_handle = nullptr;
      heif_check(heif_context_encode_image(context.get(), bitmap.get(), encoder.get(), nullptr, &raw_handle));
      handle.reset(raw_handle);
    }
    if (metadata) {
      auto exif = write_exif(info);
      heif_check(heif_context_add_exif_metadata(context.get(), handle.get(), exif.data(), int(exif.size())));
    }
    heif_writer writer{1, heif_output};
    heif_check(heif_context_write(context.get(), &writer, file));
  }
};
#endif
}

std::unique_ptr<RowWriter> row_writer(FILE* file,const pe_image_info& info,pe_format format,uint32_t quality,bool metadata){
  if(format==PE_JPEG)return std::make_unique<JpegRows>(file,info,quality,metadata);
  if(format==PE_PNG)return std::make_unique<PngRows>(file,info,metadata);
#ifdef PE_HAVE_HEIF
  if(format==PE_HEIF)return std::make_unique<HeifRows>(file,info,quality,metadata);
#endif
  throw Error(PE_UNSUPPORTED,"Requested output codec is not enabled in this build");
}

ImageProbe probe_image(const std::string& path,uint64_t file_limit){
  ImageProbe probe;std::error_code error;probe.file_bytes=std::filesystem::file_size(path,error);
  require(!error,"Cannot open image: "+path,PE_IO_ERROR);
  require(probe.file_bytes>0 && probe.file_bytes<=file_limit,"Input exceeds file size limit",PE_LIMIT_EXCEEDED);
  std::array<uint8_t,64> header{};{std::ifstream file(path,std::ios::binary);file.read(reinterpret_cast<char*>(header.data()),header.size());}
  if(header[0]==0xff && header[1]==0xd8){
    auto state=std::make_unique<JpegRead>();state->codec.err=jpeg_std_error(&state->error.manager);state->error.manager.error_exit=jpeg_error;
    if(setjmp(state->error.jump))throw Error(PE_DECODE_ERROR,state->error.message);
    jpeg_header(state.get(),path);probe.metadata=state->meta;probe.jpeg=true;probe.progressive=state->codec.progressive_mode;probe.format="JPEG";
  }else if(png_sig_cmp(header.data(),0,8)==0){
    auto integer=[&](int i){return uint32_t(header[i])<<24|uint32_t(header[i+1])<<16|uint32_t(header[i+2])<<8|header[i+3];};
    probe.metadata.info.width=integer(16);probe.metadata.info.height=integer(20);probe.format="PNG";
  }
#ifdef PE_HAVE_WEBP
  else if(std::memcmp(header.data(),"RIFF",4)==0 && std::memcmp(header.data()+8,"WEBP",4)==0){
    int w=0,h=0;require(WebPGetInfo(header.data(),header.size(),&w,&h),"Invalid WebP header",PE_DECODE_ERROR);
    probe.metadata.info.width=uint32_t(w);probe.metadata.info.height=uint32_t(h);probe.format="WebP";
  }
#endif
#ifdef PE_HAVE_HEIF
  else if(heif_file(path,probe.file_bytes)){
    std::unique_ptr<heif_context,decltype(&heif_context_free)> context(heif_context_alloc(),heif_context_free);
    require(bool(context),"Cannot allocate HEIF probe",PE_OUT_OF_MEMORY);
    heif_check(heif_context_read_from_file(context.get(),path.c_str(),nullptr));
    heif_image_handle* handle=nullptr;heif_check(heif_context_get_primary_image_handle(context.get(),&handle));
    probe.metadata.info.width=uint32_t(heif_image_handle_get_width(handle));probe.metadata.info.height=uint32_t(heif_image_handle_get_height(handle));
    heif_image_handle_release(handle);probe.format="HEIF";
  }
#endif
  else{
    LibRaw raw;const int status=raw.open_file(path.c_str());require(status==LIBRAW_SUCCESS,libraw_strerror(status),PE_UNSUPPORTED);
    probe.metadata.info.width=raw.imgdata.sizes.width;probe.metadata.info.height=raw.imgdata.sizes.height;
    checked_pixels(raw.imgdata.sizes.raw_width,raw.imgdata.sizes.raw_height);
    probe.sensor_pixels=uint64_t(raw.imgdata.sizes.raw_width)*raw.imgdata.sizes.raw_height;probe.raw=true;
  }
  checked_pixels(probe.metadata.info.width,probe.metadata.info.height);return probe;
}

std::unique_ptr<DiskImage> jpeg_to_disk(const std::string& path,const pe_decode_options& options,uint64_t budget){
  auto state=std::make_unique<JpegRead>();state->codec.err=jpeg_std_error(&state->error.manager);state->error.manager.error_exit=jpeg_error;
  auto disk=std::make_unique<DiskImage>();
  if(setjmp(state->error.jump))throw Error(PE_DECODE_ERROR,state->error.message);
  jpeg_header(state.get(),path);checked_pixels(state->codec.image_width,state->codec.image_height,options.max_pixels);
  const uint32_t width=state->codec.image_width,height=state->codec.image_height;
  const uint64_t disk_bytes=uint64_t(width)*height*8;
  const auto temporary=std::filesystem::temp_directory_path();
  require(std::filesystem::space(temporary).available>=disk_bytes+64*1024*1024,"JPEG 分条处理的临时磁盘空间不足，需要至少 "+std::to_string((disk_bytes+1048575)/1048576)+" MiB",PE_IO_ERROR);
  jpeg_decoder_budget(state.get(),budget,uint64_t(width)*64*32);
  disk->metadata=state->meta;
  std::string name=(temporary/"photo-pixels-XXXXXX").string();std::vector<char> writable(name.begin(),name.end());writable.push_back(0);
  const int fd=mkstemp(writable.data());require(fd>=0,"Cannot create private pixel storage",PE_IO_ERROR);::unlink(writable.data());
  disk->file=fdopen(fd,"w+b");if(!disk->file){::close(fd);throw Error(PE_IO_ERROR,"Cannot open pixel storage");}
  if(setjmp(state->error.jump))throw Error(PE_DECODE_ERROR,state->error.message);
  state->codec.out_color_space=JCS_RGB;jpeg_start_decompress(&state->codec);state->row.resize(size_t(width)*3);
  for(uint32_t y=0;y<height;){
    const uint32_t rows=std::min(64U,height-y);state->chunk=image(width,rows);state->pixels.resize(state->chunk->pixels.size());
    for(uint32_t n=0;n<rows;++n){JSAMPROW row=state->row.data();jpeg_read_scanlines(&state->codec,&row,1);
      for(uint32_t x=0;x<width;++x){size_t p=(size_t(n)*width+x)*4;
        for(int c=0;c<3;++c)state->pixels[p+c]=uint16_t(state->row[size_t(x)*3+c])*257;state->pixels[p+3]=65535;}
    }
    from_display(*state->chunk,state->pixels.data(),state->icc);
    require(std::fwrite(state->chunk->pixels.data(),2,state->chunk->pixels.size(),disk->file)==state->chunk->pixels.size(),"Cannot write temporary pixel storage",PE_IO_ERROR);y+=rows;
  }
  jpeg_finish_decompress(&state->codec);require(std::fflush(disk->file)==0,"Cannot flush temporary pixels",PE_IO_ERROR);return disk;
}

std::unique_ptr<pe_image> decode(const std::string& path, const pe_decode_options& options) {
  std::error_code file_error;
  const uint64_t file_size = std::filesystem::file_size(path, file_error);
  require(!file_error, "Cannot open image: " + path, PE_IO_ERROR);
  require(file_size && file_size <= options.max_file_bytes, "Input exceeds file size limit", PE_LIMIT_EXCEEDED);
  std::array<uint8_t, 16> signature{};
  { std::ifstream file(path, std::ios::binary); file.read(reinterpret_cast<char*>(signature.data()), signature.size()); }
  if(signature[0]==0xff && signature[1]==0xd8 && signature[2]==0xff) {
    auto out = jpeg_decode(path, options);
    if (options.max_edge && std::max(out->info.width,out->info.height)>options.max_edge) return resize(*out,options.max_edge);
    return out;
  }
  const bool png = png_sig_cmp(signature.data(), 0, 8) == 0;
  const bool webp = std::memcmp(signature.data(), "RIFF", 4) == 0 &&
      std::memcmp(signature.data() + 8, "WEBP", 4) == 0;
  const bool heif = !png && !webp && heif_file(path, file_size);
  std::unique_ptr<pe_image> out;
  if (!png && !webp && !heif) {
    // LibRaw opens its own file stream. Do not first copy a complete RAW into
    // RAM or apply the compressed PNG/WebP/HEIF preview-buffer budget to it.
    out = raw_decode(path, options);
  } else {
    require_memory(file_size*2, memory_budget(options.max_edge != 0), "读取压缩文件");
    Bytes data = read_file(path, options.max_file_bytes);
    if (png) out = png_decode(data, options);
    else if (webp) {
#ifdef PE_HAVE_WEBP
      out = webp_decode(data, options);
#else
      throw Error(PE_UNSUPPORTED, "WebP codec is not enabled in this build");
#endif
    } else {
#ifdef PE_HAVE_HEIF
      out = heif_decode(data, options);
#else
      throw Error(PE_UNSUPPORTED, "HEIF codec is not enabled in this build");
#endif
    }
  }
  if (options.max_edge && std::max(out->info.width, out->info.height) > options.max_edge) return resize(*out, options.max_edge);
  return out;
}

void encode(const pe_image& img, const std::string& path, pe_format format, uint32_t quality, bool metadata) {
  require(format == PE_JPEG || format == PE_PNG || format == PE_HEIF, "Unsupported output format", PE_UNSUPPORTED);
#ifndef PE_HAVE_HEIF
  require(format != PE_HEIF, "HEIF codec is not enabled in this build", PE_UNSUPPORTED);
#endif
  Destination destination(path);
  auto writer=row_writer(destination.file,img.info,format,quality,metadata);
  writer->rows(img,0,img.info.height);writer->finish();
  destination.finish();
}
}
