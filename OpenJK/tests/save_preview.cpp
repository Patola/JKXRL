#include "../code/rd-vulkan/vk_save_preview.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (0)

int main(int argc, char **argv)
{
    try {
        constexpr int w = SavePreview::Width, h = SavePreview::Height;
        std::vector<unsigned char> rgba(w*h*4, 255), rgb(w*h*3), decoded(w*h*4), jpeg(w*h*3);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                auto *p = &rgba[(y*w+x)*4];
                p[0] = y < h/2 ? 220 : 20;
                p[1] = x < w/2 ? 30 : 200;
                p[2] = y < h/2 ? 20 : 220;
                if (x < 32 && y < 32) p[0] = p[1] = p[2] = 0;
            }
        SavePreview::Resample(rgba.data(), w, h, rgb.data(), w, h, 3, false, true, true);
        const size_t length = SavePreview::EncodeJpeg(jpeg.data(), jpeg.size(), 95, w, h, rgb.data(), 0, true);
        CHECK(length > 0);
        CHECK(SavePreview::DecodeJpeg(jpeg.data(), length, decoded.data(), w, h));
        CHECK(decoded[0] < 4 && decoded[1] < 4 && decoded[2] < 4 && decoded[3] == 255);
        for (int y : {64, 400})
            for (int x : {64, 400})
                for (int c = 0; c < 4; ++c)
                    CHECK(std::abs(int(rgba[(y*w+x)*4+c])-decoded[(y*w+x)*4+c]) <= 3);
        std::fill(decoded.begin(), decoded.end(), 77);
        CHECK(!SavePreview::DecodeJpeg(nullptr, 0, decoded.data(), w, h));
        CHECK(!SavePreview::DecodeJpeg(jpeg.data(), length/2, decoded.data(), w, h));
        CHECK(!SavePreview::DecodeJpeg(jpeg.data(), length-2, decoded.data(), w, h));
        CHECK(!SavePreview::DecodeJpeg(jpeg.data(), length, decoded.data(), 1, 1));
        CHECK(!SavePreview::DecodeJpeg(jpeg.data(), SavePreview::MaxJpegBytes+1, decoded.data(), w, h));
        CHECK(std::all_of(decoded.begin(), decoded.end(), [](auto v){return v == 77;}));
        unsigned char small[65];
        std::fill(std::begin(small), std::end(small), 123);
        CHECK(SavePreview::EncodeJpeg(small, 64, 95, w, h, rgb.data(), 0, true) == 0);
        CHECK(small[64] == 123);
        CHECK(SavePreview::EncodeJpeg(jpeg.data(), jpeg.size(), 95, w, h, rgb.data(), -1, true) == 0);
        for (int n = 1; n < 200; ++n)
        {
            std::vector<unsigned char> bad(n, static_cast<unsigned char>(n));
            CHECK(!SavePreview::DecodeJpeg(bad.data(), bad.size(), decoded.data(), w, h));
        }
        // BGRA channel order, center crop and upside-down readback are independent.
        const unsigned char wide[] = {0,0,0,255, 30,20,10,255, 60,50,40,255, 0,0,0,255};
        unsigned char result[4] = {};
        SavePreview::Resample(wide, 4, 1, result, 1, 1, 4, true, false, true);
        CHECK(result[0] == 25 && result[1] == 35 && result[2] == 45 && result[3] == 255);
        for (bool bgra : {false, true})
            for (bool flip : {false, true})
            {
                SavePreview::Resample(rgba.data(), w, h, decoded.data(), w, h, 4, bgra, flip, false);
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        for (int c = 0; c < 4; ++c)
                            CHECK(decoded[(y*w+x)*4+c] ==
                                rgba[((flip ? h-1-y : y)*w+x)*4+(bgra && c<3 ? 2-c : c)]);
            }
        for (int i = 1; i < argc; ++i)
        {
            std::ifstream input(argv[i], std::ios::binary);
            CHECK(input.good());
            std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)), {});
            CHECK(SavePreview::DecodeJpeg(data.data(), data.size(), decoded.data(), w, h));
            std::printf("legacy preview accepted: %s (%zu bytes)\n", argv[i], data.size());
        }
        std::puts("save-preview codec, failure, resampling and orientation checks passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
