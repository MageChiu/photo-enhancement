#include "internal.hpp"
#include <libraw/libraw.h>
#include <jpeglib.h>
#include <png.h>
#include <lcms2.h>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#ifdef PE_HAVE_WEBP
#include <webp/decode.h>
#include <webp/demux.h>
#endif
#ifdef PE_HAVE_HEIF
#include <libheif/heif.h>
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
struct JpegError { jpeg_error_mgr manager{}; std::jmp_buf jump; char message[JMSG_LENGTH_MAX]{}; };
void jpeg_error(j_common_ptr common) {
  auto* error = reinterpret_cast<JpegError*>(common->err);
  error->manager.format_message(common, error->message);
  std::longjmp(error->jump, 1);
}
struct JpegRead {
  jpeg_decompress_struct codec{}; JpegError error; bool created = false;
  Bytes row; std::vector<uint16_t> pixels;
  Metadata meta; Bytes icc; std::array<Bytes, 256> chunks;
  ~JpegRead() { if (created) jpeg_destroy_decompress(&codec); }
};
std::unique_ptr<pe_image> jpeg_decode(const Bytes& bytes, const pe_decode_options& options) {
  auto state = std::make_unique<JpegRead>();
  state->codec.err = jpeg_std_error(&state->error.manager);
  state->error.manager.error_exit = jpeg_error;
  if (setjmp(state->error.jump)) throw Error(PE_DECODE_ERROR, state->error.message);
  jpeg_create_decompress(&state->codec); state->created = true;
  jpeg_mem_src(&state->codec, bytes.data(), static_cast<unsigned long>(bytes.size()));
  jpeg_save_markers(&state->codec, JPEG_APP0 + 1, 65535);
  jpeg_save_markers(&state->codec, JPEG_APP0 + 2, 65535);
  jpeg_read_header(&state->codec, TRUE);
  checked_pixels(state->codec.image_width, state->codec.image_height, options.max_pixels);
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
  if (options.max_edge) {
    unsigned edge = std::max(state->codec.image_width, state->codec.image_height);
    for (unsigned scale : {8U, 4U, 2U})
      if (edge / scale >= options.max_edge) { state->codec.scale_num = 1; state->codec.scale_denom = scale; break; }
  }
  state->codec.out_color_space = JCS_RGB;
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
  int result = raw.open_file(path.c_str());
  require(result == LIBRAW_SUCCESS, std::string("LibRaw: ") + libraw_strerror(result), PE_UNSUPPORTED);
  checked_pixels(raw.imgdata.sizes.raw_width, raw.imgdata.sizes.raw_height, options.max_pixels);
  raw.imgdata.params.output_bps = 16;
  raw.imgdata.params.output_color = 1;
  raw.imgdata.params.gamm[0] = 1; raw.imgdata.params.gamm[1] = 1;
  raw.imgdata.params.no_auto_bright = 1;
  raw.imgdata.params.use_camera_wb = 1;
  raw.imgdata.params.use_auto_wb = 0;
  raw.imgdata.params.user_qual = 3;
  raw.imgdata.params.half_size = options.max_edge &&
      std::max(raw.imgdata.sizes.width, raw.imgdata.sizes.height) > options.max_edge * 2ULL;
  result = raw.unpack();
  require(result == LIBRAW_SUCCESS, std::string("LibRaw unpack: ") + libraw_strerror(result), PE_DECODE_ERROR);
  result = raw.dcraw_process();
  require(result == LIBRAW_SUCCESS, std::string("LibRaw process: ") + libraw_strerror(result), PE_DECODE_ERROR);
  auto cleanup = [](libraw_processed_image_t* p) { LibRaw::dcraw_clear_mem(p); };
  std::unique_ptr<libraw_processed_image_t, decltype(cleanup)> bitmap(raw.dcraw_make_mem_image(&result), cleanup);
  require(bitmap && result == LIBRAW_SUCCESS && bitmap->type == LIBRAW_IMAGE_BITMAP && bitmap->bits == 16 && bitmap->colors >= 3,
      "LibRaw did not produce an RGB16 bitmap", PE_DECODE_ERROR);
  auto out = image(bitmap->width, bitmap->height);
  auto* data = reinterpret_cast<const uint16_t*>(bitmap->data);
  for (size_t i = 0; i < out->pixels.size() / 4; ++i) {
    std::copy_n(data + i * bitmap->colors, 3, out->pixels.data() + i * 4); out->pixels[i * 4 + 3] = 65535;
  }
  auto& info = out->info; info.is_raw = 1;
  std::snprintf(info.make, sizeof(info.make), "%s", raw.imgdata.idata.make);
  std::snprintf(info.model, sizeof(info.model), "%s", raw.imgdata.idata.model);
  info.iso = uint32_t(std::max(0.f, raw.imgdata.other.iso_speed));
  info.aperture = raw.imgdata.other.aperture; info.shutter = raw.imgdata.other.shutter;
  info.focal_length = raw.imgdata.other.focal_len;
  return out;
}

#ifdef PE_HAVE_WEBP
std::unique_ptr<pe_image> webp_decode(const Bytes& bytes, const pe_decode_options& options) {
  WebPBitstreamFeatures features{};
  require(WebPGetFeatures(bytes.data(), bytes.size(), &features) == VP8_STATUS_OK, "Invalid WebP", PE_DECODE_ERROR);
  require(!features.has_animation, "Animated WebP is not supported", PE_UNSUPPORTED);
  checked_pixels(uint32_t(features.width), uint32_t(features.height), options.max_pixels);
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
  Bytes row;
  ~JpegWrite() { if (created) jpeg_destroy_compress(&codec); }
};
void jpeg_encode(const pe_image& img, FILE* file, uint32_t quality, bool metadata) {
  auto state = std::make_unique<JpegWrite>();
  auto pixels = rgba8(img, true); auto exif = metadata ? write_exif(img.info) : Bytes{};
  if (metadata) exif.insert(exif.begin(), {'E', 'x', 'i', 'f', 0, 0});
  state->codec.err = jpeg_std_error(&state->error.manager); state->error.manager.error_exit = jpeg_error;
  if (setjmp(state->error.jump)) throw Error(PE_IO_ERROR, state->error.message);
  jpeg_create_compress(&state->codec); state->created = true;
  jpeg_stdio_dest(&state->codec, file);
  state->codec.image_width = img.info.width; state->codec.image_height = img.info.height;
  state->codec.input_components = 3; state->codec.in_color_space = JCS_RGB;
  jpeg_set_defaults(&state->codec); jpeg_set_quality(&state->codec, int(quality), TRUE);
  // 4:4:4 avoids color fringing on small border text.
  for (int c = 0; c < 3; ++c) { state->codec.comp_info[c].h_samp_factor = 1; state->codec.comp_info[c].v_samp_factor = 1; }
  jpeg_start_compress(&state->codec, TRUE);
  if (metadata) jpeg_write_marker(&state->codec, JPEG_APP0 + 1, exif.data(), static_cast<unsigned int>(exif.size()));
  state->row.resize(size_t(img.info.width) * 3);
  while (state->codec.next_scanline < img.info.height) {
    size_t start = size_t(state->codec.next_scanline) * img.info.width * 4;
    for (uint32_t x = 0; x < img.info.width; ++x) std::copy_n(pixels.data() + start + x * 4, 3, state->row.data() + x * 3);
    JSAMPROW row = state->row.data(); jpeg_write_scanlines(&state->codec, &row, 1);
  }
  jpeg_finish_compress(&state->codec);
}
void png_encode(const pe_image& img, FILE* file, bool metadata) {
  auto state = std::make_unique<PngState>(); state->writing = true;
  state->codec = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  require(state->codec, "Cannot allocate PNG encoder", PE_OUT_OF_MEMORY);
  state->info = png_create_info_struct(state->codec);
  require(state->info, "Cannot allocate PNG info", PE_OUT_OF_MEMORY);
  state->data.resize(img.pixels.size() * 2); state->rows.resize(img.info.height);
  for (size_t i = 0; i < img.pixels.size(); ++i) {
    uint16_t value = i % 4 == 3 ? img.pixels[i] : quantize(display(img.pixels[i] / 65535.0));
    state->data[i * 2] = uint8_t(value >> 8); state->data[i * 2 + 1] = uint8_t(value);
  }
  for (uint32_t y = 0; y < img.info.height; ++y) state->rows[y] = state->data.data() + size_t(y) * img.info.width * 8;
  if (metadata) state->exif = write_exif(img.info);
  if (setjmp(png_jmpbuf(state->codec))) throw Error(PE_IO_ERROR, "PNG encoding failed");
  png_init_io(state->codec, file);
  png_set_IHDR(state->codec, state->info, img.info.width, img.info.height, 16,
      PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_set_sRGB(state->codec, state->info, PNG_sRGB_INTENT_RELATIVE);
#ifdef PNG_eXIf_SUPPORTED
  if (metadata) png_set_eXIf_1(state->codec, state->info, static_cast<png_uint_32>(state->exif.size()), state->exif.data());
#endif
  png_write_info(state->codec, state->info); png_write_image(state->codec, state->rows.data()); png_write_end(state->codec, state->info);
}
#ifdef PE_HAVE_HEIF
heif_error heif_output(heif_context*, const void* data, size_t size, void* user) {
  if (std::fwrite(data, 1, size, static_cast<FILE*>(user)) != size)
    return {heif_error_Encoding_error, heif_suberror_Unspecified, "Cannot write HEIF output"};
  return {heif_error_Ok, heif_suberror_Unspecified, "Success"};
}
void heif_encode(const pe_image& img, FILE* file, uint32_t quality, bool metadata) {
  std::unique_ptr<heif_context, decltype(&heif_context_free)> context(heif_context_alloc(), heif_context_free);
  require(bool(context), "Cannot allocate HEIF context", PE_OUT_OF_MEMORY);
  heif_encoder* raw_encoder = nullptr;
  heif_check(heif_context_get_encoder_for_format(context.get(), heif_compression_HEVC, &raw_encoder));
  std::unique_ptr<heif_encoder, decltype(&heif_encoder_release)> encoder(raw_encoder, heif_encoder_release);
  heif_check(heif_encoder_set_lossy_quality(encoder.get(), int(quality)));
  heif_image* raw_image = nullptr;
  heif_check(heif_image_create(int(img.info.width), int(img.info.height), heif_colorspace_RGB, heif_chroma_interleaved_RGB, &raw_image));
  std::unique_ptr<heif_image, decltype(&heif_image_release)> bitmap(raw_image, heif_image_release);
  heif_check(heif_image_add_plane(bitmap.get(), heif_channel_interleaved, int(img.info.width), int(img.info.height), 8));
  int stride = 0; uint8_t* plane = heif_image_get_plane(bitmap.get(), heif_channel_interleaved, &stride);
  auto pixels = rgba8(img, true);
  for (uint32_t y = 0; y < img.info.height; ++y) for (uint32_t x = 0; x < img.info.width; ++x)
    std::copy_n(pixels.data() + (size_t(y) * img.info.width + x) * 4, 3, plane + size_t(y) * stride + x * 3);
  std::unique_ptr<heif_color_profile_nclx, decltype(&heif_nclx_color_profile_free)> nclx(heif_nclx_color_profile_alloc(), heif_nclx_color_profile_free);
  require(bool(nclx), "Cannot allocate HEIF color profile", PE_OUT_OF_MEMORY);
  heif_nclx_color_profile_set_color_primaries(nclx.get(), 1);
  heif_nclx_color_profile_set_transfer_characteristics(nclx.get(), 13);
  heif_nclx_color_profile_set_matrix_coefficients(nclx.get(), 6);
  heif_check(heif_image_set_nclx_color_profile(bitmap.get(), nclx.get()));
  heif_image_handle* raw_handle = nullptr;
  heif_check(heif_context_encode_image(context.get(), bitmap.get(), encoder.get(), nullptr, &raw_handle));
  std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)> handle(raw_handle, heif_image_handle_release);
  if (metadata) { auto exif = write_exif(img.info); heif_check(heif_context_add_exif_metadata(context.get(), handle.get(), exif.data(), int(exif.size()))); }
  heif_writer writer{1, heif_output}; heif_check(heif_context_write(context.get(), &writer, file));
}
#endif
}

std::unique_ptr<pe_image> decode(const std::string& path, const pe_decode_options& options) {
  Bytes data = read_file(path, options.max_file_bytes);
  std::unique_ptr<pe_image> out;
  if (data.size() >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff) out = jpeg_decode(data, options);
  else if (data.size() >= 8 && png_sig_cmp(data.data(), 0, 8) == 0) out = png_decode(data, options);
  else if (data.size() >= 12 && std::memcmp(data.data(), "RIFF", 4) == 0 && std::memcmp(data.data() + 8, "WEBP", 4) == 0) {
#ifdef PE_HAVE_WEBP
    out = webp_decode(data, options);
#else
    throw Error(PE_UNSUPPORTED, "WebP codec is not enabled in this build");
#endif
  } else {
    bool heif = false;
    if (data.size() >= 16 && std::memcmp(data.data() + 4, "ftyp", 4) == 0) {
      uint32_t box = uint32_t(data[0]) << 24 | uint32_t(data[1]) << 16 | uint32_t(data[2]) << 8 | data[3];
      require(box >= 16 && box <= data.size() && box <= 4096, "Invalid ISO BMFF file type box", PE_DECODE_ERROR);
      bool cr3 = false;
      for (uint32_t offset = 8; offset + 4 <= box; offset += 4) {
        if (offset == 12) continue; // minor version, not a brand
        const auto* brand = data.data() + offset;
        cr3 = cr3 || std::memcmp(brand, "crx ", 4) == 0;
        for (const char* name : {"heic", "heix", "hevc", "hevx", "mif1", "msf1", "avif", "avis"}) heif = heif || std::memcmp(brand, name, 4) == 0;
      }
      if (cr3) heif = false;
    }
    if (heif) {
#ifdef PE_HAVE_HEIF
      out = heif_decode(data, options);
#else
      throw Error(PE_UNSUPPORTED, "HEIF codec is not enabled in this build");
#endif
    } else {
      // Includes alternate TIFF signatures (ORF/RW2), RAF and CR3. LibRaw is
      // authoritative; no extension-based guesses or home-grown TIFF matching.
      data.clear(); data.shrink_to_fit(); out = raw_decode(path, options);
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
  if (format == PE_JPEG) jpeg_encode(img, destination.file, quality, metadata);
  else if (format == PE_PNG) png_encode(img, destination.file, metadata);
#ifdef PE_HAVE_HEIF
  else heif_encode(img, destination.file, quality, metadata);
#endif
  destination.finish();
}
}
