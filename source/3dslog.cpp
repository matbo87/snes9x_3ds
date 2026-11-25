
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "3dssettings.h"
#include "3dslog.h"

static const bool FORCE_DEBUG_LOGS = false;

// In game, lines collect in RAM and reach the SD on log3dsSetBuffered(false)
// so logging does not stall frames. Set to false when chasing a crash.
static const bool LOG_BUFFER_IN_GAME = true;

static FILE *logFile = NULL;
static char logPath[PATH_MAX];
static u64 osTime;
static u64 elapsedMs = 0;

static LightLock logLock;
static bool logBuffered = false;
static char logBuffer[32 * 1024];
static size_t logBufferLen = 0;
static u32 logDropped = 0;

static void flushBufferLocked() {
    if (logBufferLen > 0) {
        fwrite(logBuffer, 1, logBufferLen, logFile);
        logBufferLen = 0;
    }
    if (logDropped > 0) {
        fprintf(logFile, "        [log] %lu lines dropped (buffer full)\n", (unsigned long)logDropped);
        logDropped = 0;
    }
    fflush(logFile);
}

void log3dsInitialize() {
    if (logFile || (!settings3DS.LogFileEnabled && !FORCE_DEBUG_LOGS)) return;

    // only needed if we also want to debug the very first run
    if (FORCE_DEBUG_LOGS) {
        mkdir(settings3DS.RootDir, 0777);
    }

    snprintf(logPath, sizeof(logPath), "%s/debug_%s_session.log", settings3DS.RootDir, settings3dsGetAppVersion("v"));

    LightLock_Init(&logLock);
    logFile = fopen(logPath, "w"); // overwrite file on each run
    if (logFile) {
        osTime = osGetTime();
    }
}

void log3dsWrite(const char *fmt, ...) {
    if (!logFile || (!settings3DS.LogFileEnabled && !FORCE_DEBUG_LOGS)) return;

    // Lines longer than the local buffer are truncated.
    char line[512];
    int len = 0;

    LightLock_Lock(&logLock);

    u64 currentElapsedMs = osGetTime() - osTime;
    if (currentElapsedMs > elapsedMs) {
        int seconds = ((int)(currentElapsedMs / 1000)) % 100;
        int milliseconds = ((int)currentElapsedMs) % 1000;
        len = snprintf(line, sizeof(line), "[%02d.%03d] ", seconds, milliseconds);
        elapsedMs = currentElapsedMs;
    } else {
        len = snprintf(line, sizeof(line), "%s ", "       |");
    }

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line + len, sizeof(line) - len - 1, fmt, args);
    va_end(args);
    if (n > 0)
        len += (n < (int)(sizeof(line) - len - 1)) ? n : (int)(sizeof(line) - len - 2);
    line[len++] = '\n';

    if (logBuffered) {
        if (logBufferLen + len <= sizeof(logBuffer)) {
            memcpy(logBuffer + logBufferLen, line, len);
            logBufferLen += len;
        } else {
            logDropped++;
        }
    } else {
        fwrite(line, 1, len, logFile);
        fflush(logFile);
    }

    LightLock_Unlock(&logLock);
}

void log3dsSetBuffered(bool buffered) {
    if (!logFile) return;

    LightLock_Lock(&logLock);
    if (!buffered)
        flushBufferLocked();
    logBuffered = buffered && LOG_BUFFER_IN_GAME;
    LightLock_Unlock(&logLock);
}

const char *log3dsGetPath(void) {
    return logFile ? logPath : NULL;
}

void log3dsClose(void) {
    if (logFile) {
        log3dsSetBuffered(false);
        fclose(logFile);
        logFile = NULL;
    }
}

const char* log3dsGetCurrentDate() {
    static char dateFormatted[19];

    time_t seconds =  osGetTime() / 1000;
    const time_t SECONDS_BETWEEN_1900_AND_1970 = 2208988800ULL;
    time_t unix_time = seconds - SECONDS_BETWEEN_1900_AND_1970;
    struct tm *timeinfo = localtime(&unix_time);

    if (!timeinfo || strftime(dateFormatted, sizeof(dateFormatted), "%m/%d/%y %H:%M:%S", timeinfo) == 0) {
        dateFormatted[0] = '\0';
    }

    return dateFormatted;
}
