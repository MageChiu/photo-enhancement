#include "internal.hpp"
#include <lcms2.h>

namespace pe {
void from_display(pe_image& out, const uint16_t* rgba, const std::vector<uint8_t>& icc) {
  if (icc.empty()) {
    for (size_t i = 0; i < out.pixels.size(); i += 4) {
      for (int c = 0; c < 3; ++c) out.pixels[i + c] = quantize(linear(rgba[i + c] / 65535.0));
      out.pixels[i + 3] = rgba[i + 3];
    }
    return;
  }
  cmsHPROFILE input = cmsOpenProfileFromMem(icc.data(), static_cast<cmsUInt32Number>(icc.size()));
  require(input, "Embedded ICC profile is invalid", PE_DECODE_ERROR);
  if (cmsGetColorSpace(input) != cmsSigRgbData) {
    cmsCloseProfile(input);
    throw Error(PE_UNSUPPORTED, "Only RGB ICC profiles are supported");
  }
  cmsCIExyY white;
  cmsWhitePointFromTemp(&white, 6504);
  cmsCIExyYTRIPLE primaries{{.64, .33, 1}, {.30, .60, 1}, {.15, .06, 1}};
  cmsToneCurve* curve = cmsBuildGamma(nullptr, 1);
  cmsToneCurve* curves[3]{curve, curve, curve};
  cmsHPROFILE target = cmsCreateRGBProfile(&white, &primaries, curves);
  cmsFreeToneCurve(curve);
  cmsHTRANSFORM transform = cmsCreateTransform(input, TYPE_RGBA_16, target, TYPE_RGBA_16,
      INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_COPY_ALPHA | cmsFLAGS_BLACKPOINTCOMPENSATION);
  cmsCloseProfile(input); cmsCloseProfile(target);
  require(transform, "Could not create ICC transform", PE_DECODE_ERROR);
  cmsDoTransform(transform, rgba, out.pixels.data(), static_cast<cmsUInt32Number>(out.pixels.size() / 4));
  cmsDeleteTransform(transform);
}

std::vector<uint8_t> rgba8(const pe_image& img, bool flatten) {
  std::vector<uint8_t> out(img.pixels.size());
  for (size_t i = 0; i < out.size(); i += 4) {
    double alpha = img.pixels[i + 3] / 65535.0;
    for (int c = 0; c < 3; ++c) {
      double v = img.pixels[i + c] / 65535.0;
      if (flatten) v = v * alpha + 1 - alpha;
      out[i + c] = static_cast<uint8_t>(std::lround(clamp(display(v)) * 255));
    }
    out[i + 3] = flatten ? 255 : static_cast<uint8_t>((img.pixels[i + 3] + 128) / 257);
  }
  return out;
}

std::unique_ptr<pe_image> resize(const pe_image& src, uint32_t edge) {
  if (!edge || std::max(src.info.width, src.info.height) <= edge)
    return std::make_unique<pe_image>(src);
  double scale = double(edge) / std::max(src.info.width, src.info.height);
  uint32_t w = std::max(1U, uint32_t(std::lround(src.info.width * scale)));
  uint32_t h = std::max(1U, uint32_t(std::lround(src.info.height * scale)));
  auto out = image(w, h);
  out->info = src.info; out->lens = src.lens; out->info.width = w; out->info.height = h;
  // Exact area resampling in premultiplied linear light avoids transparent halos.
  for (uint32_t y = 0; y < h; ++y) {
    double y0 = double(y) * src.info.height / h, y1 = double(y + 1) * src.info.height / h;
    for (uint32_t x = 0; x < w; ++x) {
      double x0 = double(x) * src.info.width / w, x1 = double(x + 1) * src.info.width / w;
      double sum[4]{};
      for (uint32_t sy = uint32_t(y0); sy < std::min(src.info.height, uint32_t(std::ceil(y1))); ++sy)
        for (uint32_t sx = uint32_t(x0); sx < std::min(src.info.width, uint32_t(std::ceil(x1))); ++sx) {
          double weight = (std::min(x1, double(sx + 1)) - std::max(x0, double(sx))) *
                          (std::min(y1, double(sy + 1)) - std::max(y0, double(sy)));
          size_t p = (size_t(sy) * src.info.width + sx) * 4;
          double a = src.pixels[p + 3] / 65535.0;
          for (int c = 0; c < 3; ++c) sum[c] += src.pixels[p + c] / 65535.0 * a * weight;
          sum[3] += a * weight;
        }
      size_t p = (size_t(y) * w + x) * 4;
      for (int c = 0; c < 3; ++c) out->pixels[p + c] = quantize(sum[3] ? sum[c] / sum[3] : 0);
      out->pixels[p + 3] = quantize(sum[3] / ((x1 - x0) * (y1 - y0)));
    }
  }
  return out;
}

std::unique_ptr<pe_image> apply_params(const pe_image& src, const pe_params& p) {
  const double controls[]{p.contrast, p.highlights, p.shadows, p.whites, p.blacks,
      p.temperature, p.tint, p.saturation, p.vibrance};
  require(std::isfinite(p.exposure) && std::abs(p.exposure) <= 5, "Exposure must be in [-5, 5]");
  for (double v : controls) require(std::isfinite(v) && std::abs(v) <= 1, "Controls must be finite and in [-1, 1]");
  auto out = std::make_unique<pe_image>(src);
  bool identity = p.exposure == 0;
  for (double v : controls) identity = identity && v == 0;
  if (identity) return out;
  double gains[]{std::exp2(p.exposure + p.temperature * .35 + p.tint * .12),
                 std::exp2(p.exposure - p.tint * .24),
                 std::exp2(p.exposure - p.temperature * .35 + p.tint * .12)};
  for (size_t i = 0; i < out->pixels.size(); i += 4) {
    double rgb[3];
    for (int c = 0; c < 3; ++c) rgb[c] = display(clamp(src.pixels[i + c] / 65535.0 * gains[c]));
    double luma = .2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2];
    double shadow = (1 - luma) * (1 - luma), highlight = luma * luma;
    double adjustment = .35 * p.shadows * shadow + .35 * p.highlights * highlight
        + .20 * p.whites * highlight * highlight + .20 * p.blacks * shadow * shadow;
    for (double& v : rgb) v = clamp((v - .5) * std::exp2(p.contrast) + .5 + adjustment);
    luma = .2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2];
    double saturation = *std::max_element(rgb, rgb + 3) - *std::min_element(rgb, rgb + 3);
    double amount = (1 + p.saturation) * (1 + p.vibrance * (1 - saturation));
    for (int c = 0; c < 3; ++c) out->pixels[i + c] = quantize(linear(clamp(luma + (rgb[c] - luma) * amount)));
  }
  return out;
}
}
