#ifndef _PNG_UTILS_
#define _PNG_UTILS_

#include <png.h>
#include <stdio.h>
#include <3ds.h>
#include "3dslog.h"
#include "3dsfiles.h"

bool decodePngFromFile(const char* path, int& outWidth, int& outHeight);

bool savePng(const char* path, int width, int height, bool hasAlpha = false);
                       
/**
 * @brief RAII wrapper for a FILE pointer.
 * Automatically calls fclose() in its destructor.
 */
class PngFileHandle {
public:
    PngFileHandle(const char* path, const char* mode) {
        fp = file3dsOpen(path, mode);
    }
    ~PngFileHandle() {
        if (fp) {
            file3dsClose(fp);
        }
    }
    bool isOpen() const { return fp != nullptr; }
    FILE* get() const { return fp; }

    PngFileHandle(const PngFileHandle&) = delete;
    PngFileHandle& operator=(const PngFileHandle&) = delete;

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
        // Pass NULL for error handlers to enable default setjmp behavior
        png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    }
    ~PngReadHandle() {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    }
    
    bool isValid() const { return png_ptr != nullptr; }

    bool createInfo() {
        if (!png_ptr) return false;
        info_ptr = png_create_info_struct(png_ptr);
        return info_ptr != nullptr;
    }

    png_structp getPng() const { return png_ptr; }
    png_infop getInfo() const { return info_ptr; }

    PngReadHandle(const PngReadHandle&) = delete;
    PngReadHandle& operator=(const PngReadHandle&) = delete;
    
private:
    png_structp png_ptr = nullptr;
    png_infop info_ptr = nullptr;
};


class PngWriteHandle {
public:
    PngWriteHandle() {
        // Pass NULL for error handlers to enable default setjmp behavior
        png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
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
    png_structp png_ptr = nullptr;
    png_infop info_ptr = nullptr;
};

#endif
