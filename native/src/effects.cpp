#include "internal.hpp"

namespace pe {
namespace {
using RGB = std::array<double, 3>;
double wrap(double hue) { return hue - std::floor(hue); }
RGB hsl_rgb(double hue, double saturation, double lightness) {
  const double chroma = (1 - std::abs(2 * lightness - 1)) * saturation;
  const double h = wrap(hue) * 6, x = chroma * (1 - std::abs(std::fmod(h, 2) - 1));
  RGB rgb;
  if (h < 1) rgb = {chroma, x, 0};
  else if (h < 2) rgb = {x, chroma, 0};
  else if (h < 3) rgb = {0, chroma, x};
  else if (h < 4) rgb = {0, x, chroma};
  else if (h < 5) rgb = {x, 0, chroma};
  else rgb = {chroma, 0, x};
  for (auto& v : rgb) v += lightness - chroma / 2;
  return rgb;
}
double luma(const RGB& rgb) { return .2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2]; }
void range(double value, double low, double high) {
  require(std::isfinite(value) && value >= low && value <= high, "Effects parameter outside its range");
}
}

std::unique_ptr<pe_image> apply_effects(const pe_image& src, const pe_effects& e) {
  bool color = e.shadow_strength != 0 || e.highlight_strength != 0;
  for (int i = 0; i < 8; ++i) {
    range(e.hue[i], -1, 1); range(e.saturation[i], -1, 1); range(e.luminance[i], -1, 1);
    color = color || e.hue[i] != 0 || e.saturation[i] != 0 || e.luminance[i] != 0;
  }
  for (int i = 0; i < 5; ++i) { range(e.curve[i], 0, 1); color = color || e.curve[i] != i / 4.0; }
  range(e.shadow_hue, 0, 1); range(e.highlight_hue, 0, 1);
  range(e.shadow_strength, 0, 1); range(e.highlight_strength, 0, 1); range(e.balance, -1, 1);
  range(e.sharpen, 0, 2); range(e.radius, 1, 3);
  range(e.denoise_luma, 0, 1); range(e.denoise_chroma, 0, 1);
  auto out = std::make_unique<pe_image>(src);
  const auto shadow = hsl_rgb(e.shadow_hue, 1, .5), highlight = hsl_rgb(e.highlight_hue, 1, .5);
  const double centers[]{0, 30.0/360, 60.0/360, 120.0/360, 180.0/360, 240.0/360, 270.0/360, 300.0/360};
  if (color) for (size_t p = 0; p < src.pixels.size(); p += 4) {
    RGB rgb;
    for (int c = 0; c < 3; ++c) rgb[c] = display(src.pixels[p+c] / 65535.0);
    const double high = *std::max_element(rgb.begin(), rgb.end()), low = *std::min_element(rgb.begin(), rgb.end());
    const double delta = high - low, lightness = (high + low) / 2;
    if (delta > 1e-8) {
      double hue = high == rgb[0] ? (rgb[1]-rgb[2])/delta : high == rgb[1] ? (rgb[2]-rgb[0])/delta + 2 : (rgb[0]-rgb[1])/delta + 4;
      hue = wrap(hue / 6);
      double dh = 0, ds = 0, dl = 0, sum = 0;
      for (int i = 0; i < 8; ++i) {
        double distance = std::abs(hue - centers[i]); distance = std::min(distance, 1-distance);
        const double weight = std::max(0.0, 1 - distance * 6);
        dh += weight * e.hue[i]; ds += weight * e.saturation[i]; dl += weight * e.luminance[i]; sum += weight;
      }
      if (sum > 0) {
        const double saturation = delta / (1 - std::abs(2 * lightness - 1));
        rgb = hsl_rgb(hue + dh / sum / 12, clamp(saturation * (1 + ds / sum)), clamp(lightness + dl / sum * .25));
      }
    }
    for (double& value : rgb) {
      const double position = clamp(value) * 4;
      const int index = std::min(3, int(position));
      value = e.curve[index] + (e.curve[index+1] - e.curve[index]) * (position-index);
    }
    const double level = clamp(luma(rgb) + e.balance * .5);
    const double sw = e.shadow_strength * (1-level) * (1-level) * .3;
    const double hw = e.highlight_strength * level * level * .3;
    for (int c = 0; c < 3; ++c) {
      // Zero-luminance tint offsets preserve neutral exposure until clipping.
      rgb[c] += sw * (shadow[c] - luma(shadow)) + hw * (highlight[c] - luma(highlight));
      out->pixels[p+c] = quantize(linear(clamp(rgb[c])));
    }
  }
  if (e.sharpen == 0 && e.denoise_luma == 0 && e.denoise_chroma == 0) return out;
  const auto pixels = out->pixels;
  const int width = int(src.info.width), height = int(src.info.height), radius = int(std::lround(e.radius));
  auto read = [&](int x, int y) {
    const size_t p = (size_t(std::clamp(y, 0, height-1)) * width + std::clamp(x, 0, width-1)) * 4;
    return RGB{pixels[p]/65535.0, pixels[p+1]/65535.0, pixels[p+2]/65535.0};
  };
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
    const size_t p = (size_t(y)*width+x)*4;
    if (pixels[p+3] == 0) continue;
    const auto center = read(x,y); const double level = luma(center);
    RGB smooth{}; double weightSum = 0, blur = 0, blurWeights = 0;
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
      const auto sample = read(x+dx,y+dy);
      const size_t q = (size_t(std::clamp(y+dy,0,height-1))*width+std::clamp(x+dx,0,width-1))*4;
      const double alpha = pixels[q+3]/65535.0;
      const double difference = luma(sample)-level;
      const double weight = alpha * std::exp(-difference*difference/.005) * (dx==0&&dy==0 ? 4 : dx==0||dy==0 ? 2 : 1);
      for (int c=0;c<3;++c) smooth[c] += weight*sample[c];
      weightSum += weight;
      const auto sharpSample = read(x+dx*radius,y+dy*radius);
      const size_t s = (size_t(std::clamp(y+dy*radius,0,height-1))*width+std::clamp(x+dx*radius,0,width-1))*4;
      const double sharpWeight = pixels[s+3]/65535.0 * (dx==0&&dy==0 ? 4 : dx==0||dy==0 ? 2 : 1);
      blur += sharpWeight*luma(sharpSample); blurWeights += sharpWeight;
    }
    for (double& value : smooth) value /= weightSum;
    const double smoothedLuma = luma(smooth);
    const double targetLuma = level + e.denoise_luma*(smoothedLuma-level);
    const double detail = e.sharpen*(level-blur/blurWeights);
    for (int c=0;c<3;++c) {
      const double chroma = (center[c]-level)*(1-e.denoise_chroma) + (smooth[c]-smoothedLuma)*e.denoise_chroma;
      out->pixels[p+c] = quantize(targetLuma + chroma + detail);
    }
  }
  return out;
}
}
