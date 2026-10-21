#ifndef PE_H
#define PE_H

#include <stdint.h>
#if defined(_WIN32)
#define PE_API __declspec(dllexport)
#else
#define PE_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif

/* ABI v1: all owning handles must be freed by this same library. No exceptions
 * cross this boundary. Handles are immutable, and may be read concurrently.
 * Failure leaves output handles NULL. Error strings are thread-local and valid
 * until the next API call on the same thread. Paths and strings are UTF-8. */
typedef struct pe_image pe_image;
typedef struct pe_lut pe_lut;
typedef enum pe_status {
  PE_OK = 0, PE_INVALID_ARGUMENT = 1, PE_IO_ERROR = 2,
  PE_UNSUPPORTED = 3, PE_DECODE_ERROR = 4, PE_LIMIT_EXCEEDED = 5,
  PE_OUT_OF_MEMORY = 6, PE_INTERNAL_ERROR = 7
} pe_status;
typedef enum pe_format { PE_JPEG = 1, PE_PNG = 2, PE_HEIF = 3 } pe_format;

typedef struct pe_decode_options {
  uint32_t max_edge;       /* 0 = full resolution; preview typically 1600 */
  uint32_t max_pixels;     /* decoded pixel bound; scaled JPEG uses scaled size; RAW includes sensor */
  uint64_t max_file_bytes;
} pe_decode_options;

typedef struct pe_params {
  double exposure;        /* EV [-5, 5] */
  double contrast;        /* remaining controls [-1, 1], identity = 0 */
  double highlights;
  double shadows;
  double whites;
  double blacks;
  double temperature;     /* relative to decoded white balance, NOT Kelvin */
  double tint;
  double saturation;
  double vibrance;
} pe_params;

typedef struct pe_border {
  double width;           /* fraction of the original short edge [0, .25] */
  double bottom;          /* additional bottom fraction [0, .4] */
  uint32_t color;         /* 0xRRGGBB, display sRGB */
  char text[512];         /* explicit caption; empty = metadata caption */
  char font_path[1024];   /* required only for text; no OS font lookup */
} pe_border;

/* Additive frame extension; pe_border layout remains unchanged for old callers.
 * fields bits: model=1, focal=2, aperture=4, shutter=8, ISO=16, date=32,
 * custom text=64. Missing metadata is omitted. layout: centered=0, split=1,
 * stacked=2. brand: none=0, EXIF auto=1, Sony=2, Fujifilm=3, Hasselblad=4.
 * Brand names are text labels, not bundled official logo artwork.
 * Optional PNG logo: <=4MiB, <=1M pixels. */
typedef struct pe_frame {
  pe_border border;
  uint32_t fields, layout, brand;
  char logo_path[1024];
} pe_frame;

/* Additive advanced frame API; all v1 structs keep their original layout.
 * positions: logo x/y/scale, accent x/y/scale, info x/y/size/line spacing,
 * caption x/y/size. x/y in [0,1] within footer; sizes relative to footer height.
 * Text x positions its fixed-width layout box; alignment: left=0, center=1, right=2.
 * The entire artwork is clamped inside the footer. */
typedef struct pe_frame_adjustments {
  double positions[13];
  uint32_t enabled, info_align, caption_align;
  char accent_path[1024];
} pe_frame_adjustments;
typedef struct pe_lens_info { char make[128], model[256]; } pe_lens_info;
PE_API pe_status pe_read_lens_info(const char* path, uint64_t max_file_bytes, pe_lens_info* out);
PE_API pe_status pe_add_frame_advanced(const pe_image* image, const pe_frame* frame,
    const pe_frame_adjustments* adjustments, pe_image** out);

/* Additive style extension. kind: 0=ordinary LUT, 1=Hanjun. All effects are
 * blended by the existing LUT strength. Adaptation uses current decoded pixels,
 * not editing history or a claim about the scene's true white balance. */
typedef struct pe_look {
  uint32_t kind, adaptive;
  double adaptation, texture, vignette, grain; /* each [0,1] */
} pe_look;
typedef struct pe_look_analysis {
  double exposure, temperature, confidence, median; /* bounded suggestions */
} pe_look_analysis;

/* Additive ABI v1 extension. HSL centers: red, orange, yellow, green, aqua,
 * blue, purple, magenta. Curve output values at x = 0,.25,.5,.75,1.
 * Hue controls are normalized; split hues are turns [0,1]. */
typedef struct pe_effects {
  double hue[8];             /* [-1,1], maximum shift +/-30 degrees */
  double saturation[8];      /* [-1,1] */
  double luminance[8];       /* [-1,1] */
  double curve[5];           /* [0,1], piecewise-linear RGB master curve */
  double shadow_hue, shadow_strength, highlight_hue, highlight_strength;
  double balance;            /* [-1,1] */
  double sharpen, radius;    /* [0,2], radius [1,3] pixels */
  double denoise_luma, denoise_chroma; /* [0,1], edge-aware 3x3 smoothing */
} pe_effects;

typedef struct pe_image_info {
  uint32_t width;
  uint32_t height;
  uint32_t is_raw;
  uint32_t iso;
  double aperture;
  double shutter;
  double focal_length;
  char make[128];
  char model[128];
  char captured_at[64];
} pe_image_info;

PE_API uint32_t pe_abi_version(void);
/* Returns a fresh device/headroom-based working budget; preview != 0 caps
 * it for interactive work. This is an estimate, not an allocation guarantee. */
PE_API uint64_t pe_memory_budget(uint32_t preview);
PE_API const char* pe_last_error(void);
PE_API const char* pe_capabilities(void);
PE_API void pe_default_decode_options(pe_decode_options* options);
PE_API void pe_default_params(pe_params* params);
PE_API void pe_default_border(pe_border* border);
PE_API void pe_default_effects(pe_effects* effects);
PE_API pe_status pe_decode(const char* path, const pe_decode_options* options, pe_image** out);
/* RGBA16 input is LINEAR sRGB, straight alpha; copied before returning. */
PE_API pe_status pe_image_create(uint32_t width, uint32_t height, const uint16_t* rgba, pe_image** out);
PE_API pe_status pe_image_get_info(const pe_image* image, pe_image_info* info);
/* RGBA8 output is DISPLAY sRGB, straight alpha, tightly packed. */
PE_API pe_status pe_image_copy_rgba8(const pe_image* image, uint8_t* buffer, uint64_t capacity);
PE_API pe_status pe_resize(const pe_image* image, uint32_t max_edge, pe_image** out);
PE_API pe_status pe_apply_params(const pe_image* image, const pe_params* params, pe_image** out);
PE_API pe_status pe_apply_effects(const pe_image* image, const pe_effects* effects, pe_image** out);
PE_API pe_status pe_lut_load(const char* path, pe_lut** out);
PE_API pe_status pe_apply_lut(const pe_image* image, const pe_lut* lut, double strength, pe_image** out);
PE_API pe_status pe_add_border(const pe_image* image, const pe_border* border, pe_image** out);
/* Does not overwrite ANY existing file. JPEG/HEIF flatten alpha onto white.
 * Metadata is a safe whitelist; GPS, maker notes, thumbnails are never copied.
 * Encoded orientation is always 1. PNG output uses 16-bit display sRGB. */
PE_API pe_status pe_encode(const pe_image* image, const char* path, pe_format format,
                          uint32_t quality, uint32_t include_metadata);
/* Budgeted full-resolution pipeline. Baseline JPEG->JPEG/PNG can spill pixels
 * to a private temporary file and render strips (including orientation/halo).
 * memory_budget=0 selects the automatic device policy. Other formats fail
 * before full decoding if the estimated pipeline exceeds the budget. */
PE_API pe_status pe_process_file(const char* source, const char* destination,
    const pe_decode_options* options, const pe_params* params, const pe_effects* effects,
    const char* lut_path, double strength, const pe_border* border, pe_format format,
    uint32_t quality, uint32_t include_metadata, uint64_t memory_budget);
PE_API pe_status pe_add_frame(const pe_image* image, const pe_frame* frame, pe_image** out);
PE_API pe_status pe_process_file_frame(const char* source, const char* destination,
    const pe_decode_options* options, const pe_params* params, const pe_effects* effects,
    const char* lut_path, double strength, const pe_frame* frame, pe_format format,
    uint32_t quality, uint32_t include_metadata, uint64_t memory_budget);
PE_API pe_status pe_analyze_look(const pe_image* source, pe_look_analysis* analysis);
/* Region coordinates exclude the frame and describe the complete oriented
 * image. Spatial texture needs 2px input halo, in addition to earlier effects. */
PE_API pe_status pe_apply_look(const pe_image* image, const pe_lut* lut, double strength,
    const pe_look* look, const pe_look_analysis* analysis, uint32_t full_width,
    uint32_t full_height, uint32_t origin_y, pe_image** out);
PE_API pe_status pe_process_file_look(const char* source, const char* destination,
    const pe_decode_options* options, const pe_params* params, const pe_effects* effects,
    const char* lut_path, double strength, const pe_frame* frame, const pe_look* look,
    pe_format format, uint32_t quality, uint32_t include_metadata, uint64_t memory_budget);
PE_API void pe_image_free(pe_image* image);
PE_API pe_status pe_process_file_advanced(const char* source, const char* destination,
    const pe_decode_options* options, const pe_params* params, const pe_effects* effects,
    const char* lut_path, double strength, const pe_frame* frame, const pe_look* look,
    const pe_frame_adjustments* adjustments, pe_format format, uint32_t quality,
    uint32_t include_metadata, uint64_t memory_budget);
PE_API void pe_lut_free(pe_lut* lut);

#ifdef __cplusplus
}
#endif
#endif
