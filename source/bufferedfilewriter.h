#ifndef _BUFFERED_FILE_WRITER_H_
#define _BUFFERED_FILE_WRITER_H_

#include <stdio.h>
#include <string.h>
#include "3dsfiles.h"

class BufferedFileWriter {
    FILE* RawFilePointer;
    size_t Position;

public:
    BufferedFileWriter() : RawFilePointer(NULL), Position(0) {
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

    bool open(const char* filename, const char* mode) {
        RawFilePointer = file3dsOpen(filename, mode);
        if (!RawFilePointer) return false;

        // we strictly rely on the global linear heap buffer!
        // if it hasn't been allocated yet, we must fail
        if (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+')) {
            if (g_fileBuffer == NULL) {
                file3dsClose(RawFilePointer);
                RawFilePointer = NULL;
                return false; 
            }
            Position = 0;
        }
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
            return fwrite(ptr, 1, count, RawFilePointer);
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
            return rv;
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
            return success;
        }
        return true; // nothing to flush
    }
};

#endif // _BUFFERED_FILE_WRITER_H_