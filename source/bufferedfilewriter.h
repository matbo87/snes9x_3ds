#ifndef _BUFFERED_FILE_WRITER_H_
#define _BUFFERED_FILE_WRITER_H_

#include <stdio.h>
#include <string.h>
#include "3dsfiles.h"
#include "3dsutils.h"

class BufferedFileWriter {
    FILE* RawFilePointer;
    size_t Position;
    bool WriteFailed; // any short write since open()
    bool Writing;

public:
    BufferedFileWriter() : RawFilePointer(NULL), Position(0), WriteFailed(false), Writing(false) {
    }

    // safety: prevent copying
    BufferedFileWriter(const BufferedFileWriter&) = delete;
    BufferedFileWriter& operator=(const BufferedFileWriter&) = delete;

    ~BufferedFileWriter() {
        close();
    }
    
    explicit operator bool() const { 
        return RawFilePointer != NULL; 
    }

    FILE* get() const { 
        return RawFilePointer; 
    }

    // g_fileBuffer is allocated by file3dsInitialize before any writer runs
    bool open(const char* filename, const char* mode) {
        Writing = strpbrk(mode, "wa+") != NULL;
        RawFilePointer = file3dsOpen(filename, mode);
        if (!RawFilePointer) {
            if (Writing) session3DS.sdWriteFailPending = true;
            return false;
        }

        Position = 0;
        WriteFailed = false;

        return true;
    }

    // returns bytes written
    size_t write(const void* ptr, size_t count) {
        if (!RawFilePointer) return 0;

        u8* buffer = (u8*)g_fileBuffer;

        // data fits in buffer (expected scenario)
        if (Position + count <= MAX_IO_BUFFER_SIZE) {
            memcpy(buffer + Position, ptr, count);
            Position += count;
            return count;
        } 

        // buffer overflow: Flush current data (rare scencario)
        if (!flushBuffer()) {
            return 0;
        }

        // handle new data
        // write directly to disk to bypass the copy if it's huge
        if (count > MAX_IO_BUFFER_SIZE) {
            size_t written = fwrite(ptr, 1, count, RawFilePointer);
            if (written != count) WriteFailed = true;
            return written;
        }
            
        // otherwise buffer it
        memcpy(buffer, ptr, count);
        Position = count;      
        return count;
    }

    int flush() {
        if (RawFilePointer) {
            if (!flushBuffer()) return EOF;
            return fflush(RawFilePointer);
        }
        return EOF;
    }

    int close() {
        if (RawFilePointer) {
            flushBuffer();
            int rv = file3dsClose(RawFilePointer);
            
            RawFilePointer = NULL;
            Position = 0;
            if (WriteFailed || rv != 0) {
                if (Writing) session3DS.sdWriteFailPending = true;
                return EOF;
            }
            return 0;
        }
        return 0; // closing a closed file is technically a success
    }

private:
    bool flushBuffer() {
        // trust the caller: RawFilePointer is valid here
        if (Position > 0) {
            // write directly from the global linear heap buffer
            size_t written = fwrite(g_fileBuffer, 1, Position, RawFilePointer);
            bool success = (written == Position);
            Position = 0;
            if (!success) WriteFailed = true;
            return success;
        }
        return true; // nothing to flush
    }
};

#endif // _BUFFERED_FILE_WRITER_H_
