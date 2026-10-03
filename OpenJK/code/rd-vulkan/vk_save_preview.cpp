#include "vk_save_preview.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csetjmp>
#include <jpeglib.h>

namespace SavePreview {
namespace {
struct Error {
    jpeg_error_mgr jpeg;
    jmp_buf jump;
};
void Fail(j_common_ptr jpeg) { longjmp(reinterpret_cast<Error *>(jpeg->err)->jump, 1); }
void Quiet(j_common_ptr) {}
void DestinationInit(j_compress_ptr) {}
boolean DestinationFull(j_compress_ptr jpeg) { Fail(reinterpret_cast<j_common_ptr>(jpeg)); return FALSE; }
void DestinationDone(j_compress_ptr) {}

// Keep libjpeg's mutable state on the heap: automatic objects modified after
// setjmp would have indeterminate values after longjmp. No C++ destructors are
// crossed by the error handler, and neither malformed data nor a full buffer exits.
struct Encoder { jpeg_compress_struct jpeg; Error error; jpeg_destination_mgr dest; };
struct Decoder { jpeg_decompress_struct jpeg; Error error; unsigned char *pixels; };
}

size_t EncodeJpeg(unsigned char *dest, size_t capacity, int quality, int width,
                  int height, const unsigned char *rgb, int padding, bool flipVertical)
{
    if (!dest || !rgb || !capacity || capacity > MaxJpegBytes ||
        width != Width || height != Height || padding < 0 || padding > 4096) return 0;
    auto *state = static_cast<Encoder *>(std::calloc(1, sizeof(Encoder)));
    if (!state) return 0;
    state->jpeg.err = jpeg_std_error(&state->error.jpeg);
    state->error.jpeg.error_exit = Fail;
    state->error.jpeg.output_message = Quiet;
    if (setjmp(state->error.jump))
    {
        jpeg_destroy_compress(&state->jpeg);
        std::free(state);
        return 0;
    }
    jpeg_create_compress(&state->jpeg);
    state->dest.init_destination = DestinationInit;
    state->dest.empty_output_buffer = DestinationFull;
    state->dest.term_destination = DestinationDone;
    state->dest.next_output_byte = dest;
    state->dest.free_in_buffer = capacity;
    state->jpeg.dest = &state->dest;
    state->jpeg.image_width = width;
    state->jpeg.image_height = height;
    state->jpeg.input_components = 3;
    state->jpeg.in_color_space = JCS_RGB;
    jpeg_set_defaults(&state->jpeg);
    jpeg_set_quality(&state->jpeg, std::clamp(quality, 1, 100), TRUE);
    if (quality >= 85)
    {
        state->jpeg.comp_info[0].h_samp_factor = 1;
        state->jpeg.comp_info[0].v_samp_factor = 1;
    }
    jpeg_start_compress(&state->jpeg, TRUE);
    while (state->jpeg.next_scanline < state->jpeg.image_height)
    {
        const int y = flipVertical ? state->jpeg.next_scanline : height-1-state->jpeg.next_scanline;
        auto *row = const_cast<unsigned char *>(rgb + size_t(y)*(width*3+padding));
        jpeg_write_scanlines(&state->jpeg, &row, 1);
    }
    jpeg_finish_compress(&state->jpeg);
    const size_t length = capacity-state->dest.free_in_buffer;
    jpeg_destroy_compress(&state->jpeg);
    std::free(state);
    return length;
}

bool DecodeJpeg(const unsigned char *src, size_t length, unsigned char *rgba, int width, int height)
{
    if (!src || !rgba || !length || length > MaxJpegBytes || width != Width || height != Height) return false;
    auto *state = static_cast<Decoder *>(std::calloc(1, sizeof(Decoder)));
    if (!state) return false;
    state->jpeg.err = jpeg_std_error(&state->error.jpeg);
    state->error.jpeg.error_exit = Fail;
    state->error.jpeg.output_message = Quiet;
    if (setjmp(state->error.jump))
    {
        jpeg_destroy_decompress(&state->jpeg);
        std::free(state->pixels);
        std::free(state);
        return false;
    }
    jpeg_create_decompress(&state->jpeg);
    jpeg_mem_src(&state->jpeg, src, length);
    jpeg_read_header(&state->jpeg, TRUE);
    if (state->jpeg.image_width != unsigned(width) || state->jpeg.image_height != unsigned(height))
    {
        jpeg_destroy_decompress(&state->jpeg);
        std::free(state);
        return false;
    }
    state->jpeg.out_color_space = JCS_RGB;
    jpeg_start_decompress(&state->jpeg);
    state->pixels = static_cast<unsigned char *>(std::malloc(size_t(width)*height*3));
    if (!state->pixels) Fail(reinterpret_cast<j_common_ptr>(&state->jpeg));
    while (state->jpeg.output_scanline < state->jpeg.output_height)
    {
        auto *row = state->pixels + size_t(height-1-state->jpeg.output_scanline)*width*3;
        jpeg_read_scanlines(&state->jpeg, &row, 1);
    }
    jpeg_finish_decompress(&state->jpeg);
    const bool valid = state->error.jpeg.num_warnings == 0;
    if (valid)
        for (size_t i = 0; i < size_t(width)*height; ++i)
        {
            std::memcpy(rgba+i*4, state->pixels+i*3, 3);
            rgba[i*4+3] = 255;
        }
    jpeg_destroy_decompress(&state->jpeg);
    std::free(state->pixels);
    std::free(state);
    return valid;
}
}
