#ifndef _BUFFERED_FILE_WRITER_H_
#define _BUFFERED_FILE_WRITER_H_

#include <stdio.h>
#include <string.h>
#include "3dsfiles.h"
#include "3dsutils.h"

class BufferedFileWriter {
    FILE* RawFilePointer;
    u8* MemoryTarget; // set by openMemory(); replaces the file and g_fileBuffer
    size_t MemoryCapacity;
    size_t Position;
    bool WriteFailed; // any write failure, including memory overflow
    bool Writing;

public:
    BufferedFileWriter() : RawFilePointer(NULL), MemoryTarget(NULL), MemoryCapacity(0), Position(0), WriteFailed(false), Writing(false) {
    }

    // safety: prevent copying
    BufferedFileWriter(const BufferedFileWriter&) = delete;
    BufferedFileWriter& operator=(const BufferedFileWriter&) = delete;

    ~BufferedFileWriter() {
        close();
    }
    
    explicit operator bool() const { 
        return RawFilePointer != NULL || MemoryTarget != NULL; 
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

    // Writes into buffer instead of a file; overflow fails the stream.
    void openMemory(u8* buffer, size_t capacity) {
        MemoryTarget = buffer;
        MemoryCapacity = capacity;
        Position = 0;
        WriteFailed = false;
        Writing = false;
    }

    // bytes held by a memory target; read before close()
    size_t memorySize() const {
        return MemoryTarget ? Position : 0;
    }

    // returns bytes written
    size_t write(const void* ptr, size_t count) {
        if (MemoryTarget) {
            if (WriteFailed || count > MemoryCapacity - Position) {
                WriteFailed = true;
                return 0;
            }
            memcpy(MemoryTarget + Position, ptr, count);
            Position += count;
            return count;
        }

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
        if (MemoryTarget) return 0;
        if (RawFilePointer) {
            if (!flushBuffer()) return EOF;
            return fflush(RawFilePointer);
        }
        return EOF;
    }

    int close() {
        if (MemoryTarget) {
            MemoryTarget = NULL;
            Position = 0;
            return WriteFailed ? EOF : 0;
        }
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
