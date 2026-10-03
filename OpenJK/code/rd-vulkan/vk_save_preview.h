#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace SavePreview {
constexpr int Width = 512;
constexpr int Height = 512;
constexpr size_t MaxJpegBytes = 4 * 1024 * 1024;

// Stored JKO JPEG scanlines are bottom-up; the loader reverses them for UI use.
size_t EncodeJpeg(unsigned char *dest, size_t capacity, int quality, int width,
                  int height, const unsigned char *rgb, int padding, bool flipVertical);
bool DecodeJpeg(const unsigned char *src, size_t length, unsigned char *rgba,
                int width, int height);

// Capture one eye without stretching its aspect ratio. Do not gamma-correct
// display-ready swapchain bytes again, including mutable UNORM/sRGB views.
inline void Resample(const unsigned char *src, int sw, int sh,
                     unsigned char *dest, int dw, int dh, int channels,
                     bool bgra, bool flip, bool crop, float displayAspect = 0.0f)
{
    if (!crop && sw == dw && sh == dh)
    {
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x)
            {
                const auto *s = src + (size_t(y)*sw+x)*4;
                auto *d = dest + (size_t(flip ? dh-1-y : y)*dw+x)*channels;
                d[0] = s[bgra ? 2 : 0]; d[1] = s[1]; d[2] = s[bgra ? 0 : 2];
                if (channels == 4) d[3] = 255;
            }
        return;
    }
    const float aspect = displayAspect > 0 ? displayAspect : float(dw)/dh;
    const float width = crop ? std::min(float(sw), sh*aspect) : float(sw);
    const float height = crop ? width/aspect : float(sh);
    const float left = (sw-width)*0.5f, top = (sh-height)*0.5f;
    for (int y = 0; y < dh; ++y)
        for (int x = 0; x < dw; ++x)
        {
            unsigned sum[3] = {};
            for (int sy = 0; sy < 3; ++sy)
                for (int sx = 0; sx < 4; ++sx)
                {
                    const int ix = std::min(sw-1, int(left+(x+(sx+.5f)/4)*width/dw));
                    const int iy = std::min(sh-1, int(top+(y+(sy+.5f)/3)*height/dh));
                    const auto *p = src + (size_t(iy)*sw+ix)*4;
                    sum[0] += p[bgra ? 2 : 0]; sum[1] += p[1]; sum[2] += p[bgra ? 0 : 2];
                }
            auto *p = dest + (size_t(flip ? dh-1-y : y)*dw+x)*channels;
            for (int c = 0; c < 3; ++c) p[c] = static_cast<unsigned char>(sum[c]/12);
            if (channels == 4) p[3] = 255;
        }
}
}
