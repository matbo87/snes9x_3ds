#include "png_utils.h"
#include <stdlib.h>
#include <vector>

static const long MAX_FILE_SIZE = 1024 * 1024 * 3; // 3 MB

bool decodePngFromFile(u8* targetBuffer, size_t targetBufferSize,
                       const char* path, int& outWidth, int& outHeight) {

    if (path == nullptr) {
        return false;
    }

    FileHandle file(path, "rb");
    if (!file.isOpen()) {
        log3dsWrite("Failed to open PNG file");
        return false;
    }

    u8 header[8];
    FILE* fp = file.get();

    // check Signature 
    if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8)) {
        log3dsWrite("Not a valid PNG file");
        return false;
    }
    
    // check File Size
    fseek(fp, 0, SEEK_END); 
    long fileSize = ftell(fp);
    rewind(fp); 

    if (fileSize > MAX_FILE_SIZE) {
        log3dsWrite("PNG too large: %ld bytes", fileSize);
        return false;
    }

    PngReadHandle png;
    if (!png.isValid()) {
        log3dsWrite("png_create_read_struct failed");
        return false;
    }
    if (!png.createInfo()) {
        log3dsWrite("png_create_info_struct failed");
        return false;
    }

    png_init_io(png.getPng(), file.get());
    png_read_info(png.getPng(), png.getInfo());

    outWidth = png_get_image_width(png.getPng(), png.getInfo());
    outHeight = png_get_image_height(png.getPng(), png.getInfo());
    
    png_byte color_type = png_get_color_type(png.getPng(), png.getInfo());
    png_byte bit_depth = png_get_bit_depth(png.getPng(), png.getInfo());

    // --- !!! force rgba8 output !!! ---
    
    // Strip 16-bit-per-channel images down to 8-bit
    if (bit_depth == 16)
        png_set_strip_16(png.getPng());

    // Expand 1, 2, or 4-bit packed pixel data to 8-bit
    if (bit_depth < 8)
        png_set_packing(png.getPng());

    // Expand paletted images to full RGB
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png.getPng());

    // Expand grayscale images to 8-bit
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png.getPng());

    // Expand grayscale to RGB
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png.getPng());

    // Check if the image has an alpha channel
    bool has_alpha = (color_type & PNG_COLOR_MASK_ALPHA);
    
    // Check for a tRNS (simple transparency) chunk
    bool has_trns = png_get_valid(png.getPng(), png.getInfo(), PNG_INFO_tRNS);

    if (has_trns) {
        // If it has a tRNS chunk, expand it to a full alpha channel.
        png_set_tRNS_to_alpha(png.getPng());
    } else if (!has_alpha) {
        // If it has NO native alpha channel AND NO tRNS chunk,
        // we must add a full opaque alpha channel.
        png_set_add_alpha(png.getPng(), 0xFF, PNG_FILLER_AFTER);
    }
    
    // --- END transformations to force rgba8 outpu ---
    
    if (targetBufferSize < (size_t)(outWidth * outHeight * 4)) {
        log3dsWrite("PNG decode: target buffer is too small for %s", path);
        return false; 
    }

    std::vector<png_bytep> row_pointers(outHeight);
    for (int y = 0; y < outHeight; y++) {
        row_pointers[y] = targetBuffer + (y * outWidth * 4);
    }

    // apply all transforms and read the image
    png_read_image(png.getPng(), row_pointers.data());

    return true;
}

/**
 * @brief Saves a 32-bit RGBA8 pixel buffer to a PNG file.
 *
 * @param path The path to save the PNG file (e.g., "/snes9x/screenshots/img.png").
 * @param width The width of the image.
 * @param height The height of the image.
 * @param imageData Pointer to the raw RGBA8 pixel data.
 * @return true on success, false on failure.
 */
bool savePng(const char* path, int width, int height, const void* imageData) {
    
    // --- 1. Acquire Resources ---
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

    // --- 2. Set up PNG Info ---
    png_init_io(png.getPng(), file.get());

    // Set the PNG header info. We are saving as 32-bit RGBA8.
    png_set_IHDR(
        png.getPng(),
        png.getInfo(),
        width,
        height,
        8, // 8 bits per channel
        PNG_COLOR_TYPE_RGBA, // 32-bit RGBA
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );

    // Write the info header
    png_write_info(png.getPng(), png.getInfo());

    // --- 3. Set up Row Pointers ---
    // libpng needs an array of pointers, where each pointer
    // points to the beginning of a row in your image data.
    // We use png_const_bytep because our input imageData is const.
    std::vector<png_const_bytep> row_pointers(height);
    int bytes_per_row = width * 4; // 4 bytes for RGBA

    for (int y = 0; y < height; y++) {
        row_pointers[y] = (png_const_bytep)imageData + (y * bytes_per_row);
    }

    // --- 4. Write Image Data ---
    // This call writes the entire image in one go.
    png_write_image(png.getPng(), const_cast<png_bytepp>(row_pointers.data()));

    // --- 5. Finish ---
    // This writes the end of the file.
    png_write_end(png.getPng(), NULL);

    return true;

    // Destructors for PngWriteHandle and FileHandle
    // will automatically clean up (close the file, etc.)
}