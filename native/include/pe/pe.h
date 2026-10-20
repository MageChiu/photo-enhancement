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
  uint32_t max_pixels;     /* checked before allocation, including RAW sensor */
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
PE_API const char* pe_last_error(void);
PE_API const char* pe_capabilities(void);
PE_API void pe_default_decode_options(pe_decode_options* options);
PE_API void pe_default_params(pe_params* params);
PE_API void pe_default_border(pe_border* border);
PE_API pe_status pe_decode(const char* path, const pe_decode_options* options, pe_image** out);
/* RGBA16 input is LINEAR sRGB, straight alpha; copied before returning. */
PE_API pe_status pe_image_create(uint32_t width, uint32_t height, const uint16_t* rgba, pe_image** out);
PE_API pe_status pe_image_get_info(const pe_image* image, pe_image_info* info);
/* RGBA8 output is DISPLAY sRGB, straight alpha, tightly packed. */
PE_API pe_status pe_image_copy_rgba8(const pe_image* image, uint8_t* buffer, uint64_t capacity);
PE_API pe_status pe_resize(const pe_image* image, uint32_t max_edge, pe_image** out);
PE_API pe_status pe_apply_params(const pe_image* image, const pe_params* params, pe_image** out);
PE_API pe_status pe_lut_load(const char* path, pe_lut** out);
PE_API pe_status pe_apply_lut(const pe_image* image, const pe_lut* lut, double strength, pe_image** out);
PE_API pe_status pe_add_border(const pe_image* image, const pe_border* border, pe_image** out);
/* Does not overwrite ANY existing file. JPEG/HEIF flatten alpha onto white.
 * Metadata is a safe whitelist; GPS, maker notes, thumbnails are never copied.
 * Encoded orientation is always 1. PNG output uses 16-bit display sRGB. */
PE_API pe_status pe_encode(const pe_image* image, const char* path, pe_format format,
                          uint32_t quality, uint32_t include_metadata);
PE_API void pe_image_free(pe_image* image);
PE_API void pe_lut_free(pe_lut* lut);

#ifdef __cplusplus
}
#endif
#endif
