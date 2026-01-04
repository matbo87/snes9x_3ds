#include "png_utils.h"
#include <stdlib.h>
#include <vector>

bool decodePngFromFile(u8* targetBuffer, size_t targetBufferSize, const char* path, int& outWidth, int& outHeight) {
    if (!path || !targetBuffer) return false;

    FileHandle file(path, "rb");
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

    png_init_io(png.getPng(), fp);
    png_set_sig_bytes(png.getPng(), 8);
    png_read_info(png.getPng(), png.getInfo());
    
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

    if (targetBufferSize < requiredSize) {
        log3dsWrite("Buffer too small for %s. Need %zu", path, requiredSize);
        return false;
    }

    // decode
    std::vector<png_bytep> row_pointers(outHeight);
    for (int y = 0; y < outHeight; y++) {
        row_pointers[y] = targetBuffer + (y * rowBytes);
    }

    png_read_image(png.getPng(), row_pointers.data());

    return true;
}
bool savePng(const char* path, int width, int height, const void* imageData, bool hasAlpha) {
    FileHandle file(path, "wb"); // "wb" = Write Binary
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

    // libpng needs an array of pointers, where each pointer
    // points to the beginning of a row in your image data.
    // We use png_const_bytep because our input imageData is const.
    std::vector<png_const_bytep> row_pointers(height);
    int bytes_per_row = width * (hasAlpha ? 4 : 3);

    for (int y = 0; y < height; y++) {
        row_pointers[y] = (png_const_bytep)imageData + (y * bytes_per_row);
    }

    png_write_image(png.getPng(), const_cast<png_bytepp>(row_pointers.data()));
    png_write_end(png.getPng(), NULL);

    return true;

    // Destructors for PngWriteHandle and FileHandle
    // will automatically clean up (close the file, etc.)
}