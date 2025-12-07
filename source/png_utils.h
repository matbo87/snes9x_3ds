
#include <png.h>
#include <stdio.h>
#include <3ds.h>
#include "3dslog.h"

/**
 * @brief Decodes a PNG from a file path directly into a target pixel buffer.
 *
 * @param targetBuffer The pre-allocated buffer (e.g., g_png_decode_buffer) to write pixels into.
 * @param targetBufferSize The total size in bytes of the targetBuffer.
 * @param path The path to the PNG file (e.g., "romfs:/my_image.png").
 * @param outWidth [out] The width of the decoded image.
 * @param outHeight [out] The height of the decoded image.
 * @return true on success, false on failure.
 */
bool decodePngFromFile(u8* targetBuffer, size_t targetBufferSize,
                       const char* path, int& outWidth, int& outHeight);

/**
 * @brief Saves a 32-bit RGBA8 pixel buffer to a PNG file.
 *
 * @param path The path to save the PNG file
 * @param width The width of the image.
 * @param height The height of the image.
 * @param imageData Pointer to the raw RGBA8 pixel data.
 * @return true on success, false on failure.
 */
bool savePng(const char* path, int width, int height, const void* imageData);
                       
/**
 * @brief RAII wrapper for a FILE pointer.
 * Automatically calls fclose() in its destructor.
 */
class FileHandle {
public:
    FileHandle(const char* path, const char* mode) {
        fp = fopen(path, mode);
    }
    ~FileHandle() {
        if (fp) {
            fclose(fp);
        }
    }
    // Check if the file was successfully opened
    bool isOpen() const { return fp != nullptr; }
    // Get the raw pointer
    FILE* get() const { return fp; }

    // Disable copying
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

private:
    FILE* fp = nullptr;
};

/**
 * @brief RAII wrapper for libpng's read struct.
 * Automatically calls png_destroy_read_struct() in its destructor.
 */
class PngReadHandle {
public:
    PngReadHandle() {
        // We must pass our error handler to the create function
        png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, png_error_fn, NULL);
    }
    ~PngReadHandle() {
        // This function safely handles if info_ptr is null
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    }
    
    // Check if the main struct was created
    bool isValid() const { return png_ptr != nullptr; }

    // Create the info struct
    bool createInfo() {
        if (!png_ptr) return false;
        info_ptr = png_create_info_struct(png_ptr);
        return info_ptr != nullptr;
    }

    // Get the raw pointers
    png_structp getPng() const { return png_ptr; }
    png_infop getInfo() const { return info_ptr; }

    // Disable copying
    PngReadHandle(const PngReadHandle&) = delete;
    PngReadHandle& operator=(const PngReadHandle&) = delete;

    
private:
    // This is the C-style error handler libpng requires
    static void png_error_fn(png_structp png_ptr, png_const_charp error_msg) {
        log3dsWrite("PNG Error: %s\n", error_msg);
    }

    png_structp png_ptr = nullptr;
    png_infop info_ptr = nullptr;
};


class PngWriteHandle {
public:
    PngWriteHandle() {
        png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, png_error_fn, NULL);
    }
    ~PngWriteHandle() {
        png_destroy_write_struct(&png_ptr, &info_ptr);
    }
    
    bool isValid() const { return png_ptr != nullptr; }

    bool createInfo() {
        if (!png_ptr) return false;
        info_ptr = png_create_info_struct(png_ptr);
        return info_ptr != nullptr;
    }

    png_structp getPng() const { return png_ptr; }
    png_infop getInfo() const { return info_ptr; }

    PngWriteHandle(const PngWriteHandle&) = delete;
    PngWriteHandle& operator=(const PngWriteHandle&) = delete;

private:
    // This is the C-style error handler libpng requires
    static void png_error_fn(png_structp png_ptr, png_const_charp error_msg) {
        log3dsWrite("PNG Error: %s", error_msg);
        svcBreak(USERBREAK_PANIC);
    }

    png_structp png_ptr = nullptr;
    png_infop info_ptr = nullptr;
};