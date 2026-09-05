#include "png_utils.h"
#include <stdlib.h>
#include <vector>

// Runs the shared normalize-to-RGBA transforms and decodes into g_fileBuffer.
// png_read_info must already have been called. On a libpng error the caller's
// setjmp handler unwinds here, so this must be reached through a setjmp guard.
static bool pngTransformAndDecode(PngReadHandle& png, int& outWidth, int& outHeight) {
    outWidth = png_get_image_width(png.getPng(), png.getInfo());
    outHeight = png_get_image_height(png.getPng(), png.getInfo());

    png_byte bit_depth = png_get_bit_depth(png.getPng(), png.getInfo());
    png_byte color_type = png_get_color_type(png.getPng(), png.getInfo());

    // normalize to 8-bit per channel
    if (bit_depth == 16) png_set_strip_16(png.getPng());
    if (bit_depth < 8)   png_set_packing(png.getPng());
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png.getPng());

    // expand grayscale to RGB
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        if (bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png.getPng());
        png_set_gray_to_rgb(png.getPng());
    }

    // handle transparency chunks
    if (png_get_valid(png.getPng(), png.getInfo(), PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png.getPng());
    }

    // If RGB, add opaque alpha (0xFF) at the end
    if (!(color_type & PNG_COLOR_MASK_ALPHA)) {
        png_set_add_alpha(png.getPng(), 0xFF, PNG_FILLER_AFTER);
    }

    // apply transforms
    png_read_update_info(png.getPng(), png.getInfo());

    size_t rowBytes = png_get_rowbytes(png.getPng(), png.getInfo());
    size_t requiredSize = rowBytes * outHeight;

    if (requiredSize > MAX_IO_BUFFER_SIZE) {
        log3dsWrite("PNG too large to decode: %zu (max allowed: %zu)", requiredSize, (size_t)MAX_IO_BUFFER_SIZE);
        return false;
    }

    for (int y = 0; y < outHeight; y++) {
        png_bytep rowPointer = g_fileBuffer + (y * rowBytes);
        png_read_row(png.getPng(), rowPointer, NULL);
    }

    return true;
}

bool decodePngFromFile(const char* path, int& outWidth, int& outHeight) {
    if (!path || !g_fileBuffer) return false;

    PngFileHandle file(path, "rb");
    if (!file.isOpen()) {
        log3dsWrite("Failed to open PNG: %s", path);
        return false;
    }

    FILE* fp = file.get();
    u8 header[8];

    if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8)) {
        log3dsWrite("Not a valid PNG file: %s", path);
        return false;
    }

    PngReadHandle png;
    if (!png.isValid() || !png.createInfo()) return false;

    // capture libpng errors here. If a crash happens inside libpng,
    // it jumps back to this 'if' and we return false cleanly.
    if (setjmp(png_jmpbuf(png.getPng()))) {
        log3dsWrite("PNG decode error occurred for: %s", path);
        return false;
    }

    png_init_io(png.getPng(), fp);
    png_set_sig_bytes(png.getPng(), 8);
    png_read_info(png.getPng(), png.getInfo());

    return pngTransformAndDecode(png, outWidth, outHeight);
}

struct PngMemSource {
    const u8* data;
    size_t    length;
    size_t    pos;
};

static void pngMemRead(png_structp png, png_bytep out, png_size_t want) {
    PngMemSource* src = (PngMemSource*)png_get_io_ptr(png);
    size_t avail = src->length - src->pos;
    if (want > avail) {
        png_error(png, "read past end of PNG buffer");
        return;
    }
    memcpy(out, src->data + src->pos, want);
    src->pos += want;
}

bool decodePngFromMemory(const u8* data, size_t length, int& outWidth, int& outHeight) {
    if (!data || length < 8 || !g_fileBuffer) return false;

    if (png_sig_cmp((png_const_bytep)data, 0, 8)) {
        log3dsWrite("Not a valid PNG buffer");
        return false;
    }

    PngReadHandle png;
    if (!png.isValid() || !png.createInfo()) return false;

    if (setjmp(png_jmpbuf(png.getPng()))) {
        log3dsWrite("PNG decode error occurred (memory)");
        return false;
    }

    PngMemSource src = { data, length, 0 };
    png_set_read_fn(png.getPng(), &src, pngMemRead);
    png_read_info(png.getPng(), png.getInfo());

    return pngTransformAndDecode(png, outWidth, outHeight);
}
bool savePng(const char* path, int width, int height, bool hasAlpha) {
    if (!path || !g_fileBuffer) return false;

    int bytes_per_pixel = hasAlpha ? 4 : 3;
    size_t requiredSize = (size_t)width * height * bytes_per_pixel;

    if (!requiredSize || requiredSize > MAX_IO_BUFFER_SIZE) {
        log3dsWrite("required buffer size invalid for %s: %zu (max allowed: %zu)", path, requiredSize, MAX_IO_BUFFER_SIZE);
        return false;
    }

    PngFileHandle file(path, "wb");
    if (!file.isOpen()) {
        log3dsWrite("Failed to open file for writing: %s", path);
        return false;
    }

    PngWriteHandle png;
    if (!png.isValid()) {
        log3dsWrite("png_create_write_struct failed");
        return false;
    }
    if (!png.createInfo()) {
        log3dsWrite("png_create_info_struct failed");
        return false;
    }

    // capture libpng errors
    if (setjmp(png_jmpbuf(png.getPng()))) {
        log3dsWrite("PNG write error: %s", path);
        return false;
    }

    png_init_io(png.getPng(), file.get());

    // speed things up by reducing compression level
    png_set_compression_level(png.getPng(), 1);
    
    png_set_IHDR(
        png.getPng(),
        png.getInfo(),
        width,
        height,
        8,
        hasAlpha ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );

    png_write_info(png.getPng(), png.getInfo());

    int stride = width * bytes_per_pixel;
    const u8* srcData = g_fileBuffer;

    for (int y = 0; y < height; y++) {
        // cast needed because libpng expects non-const, though it doesn't modify it
        png_write_row(png.getPng(), (png_bytep)(srcData + (y * stride)));
    }

    png_write_end(png.getPng(), NULL);

    return true;
}