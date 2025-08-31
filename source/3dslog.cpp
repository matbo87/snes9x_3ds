
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include "3dssettings.h"
#include "3dslog.h"

static FILE *logFile = NULL;
static u64 osTime;
static u64 elapsedMs = 0;

void setElapsedTime(u64 ms) {
    char timestamp[16];
    int seconds = ((int)(ms / 1000)) % 100;
    int milliseconds = ((int)ms) % 1000;

    snprintf(timestamp, sizeof(timestamp) - 1, "[%02d.%03d]", seconds, milliseconds);
    fprintf(logFile, "%s ", timestamp);
}

void log3dsInitialize() {
    if (logFile || !settings3DS.LogFileEnabled) return;

    char filepath[_MAX_PATH];
    const char *version = getAppVersion("v");
    snprintf(filepath, _MAX_PATH - 1, "%s/debug_%s_session.log", settings3DS.RootDir, version);

    logFile = fopen(filepath, "w"); // overwrite file on each run
    if (logFile) {
        osTime = osGetTime();
    }
}

void log3dsWrite(const char *fmt, ...) {
    if (!logFile) return;

    u64 currentElapsedMs = osGetTime() - osTime;

    if (currentElapsedMs > elapsedMs) {
        setElapsedTime(currentElapsedMs);
        elapsedMs = currentElapsedMs;
    } else {
        fprintf(logFile, "%s ", "       |");
    }
    
    va_list args;
    va_start(args, fmt);
    vfprintf(logFile, fmt, args);
    fprintf(logFile, "\n");
    fflush(logFile);
    va_end(args);
}

void log3dsClose(void) {
    if (logFile) {
        fclose(logFile);
        logFile = NULL;
    }
}

const char* log3dsGetCurrentDate() {
    static char dateFormatted[19];

    u64 ms = osGetTime();
    time_t seconds =  osGetTime() / 1000;
    const time_t SECONDS_BETWEEN_1900_AND_1970 = 2208988800ULL;
    time_t unix_time = seconds - SECONDS_BETWEEN_1900_AND_1970;
    struct tm *timeinfo = localtime(&unix_time);

    //  MM/DD/YY HH:mm
    snprintf(dateFormatted, 19, "%02d/%02d/%02d %02d:%02d:%02d",
             timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_year % 100, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

    return dateFormatted;
}