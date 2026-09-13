#ifndef _3DSLOG_H
#define _3DSLOG_H

void log3dsInitialize();
void log3dsWrite(const char *fmt, ...);
void log3dsClose(void);
const char* log3dsGetCurrentDate();

#define DUMP_VECTOR_INFO(name, vec) \
    log3dsWrite("%s: size=%zu, cap=%zu, data=%p", name, vec.size(), vec.capacity(), vec.data())

#define DUMP_UNORDERED_MAP_INFO(name, map) \
    log3dsWrite("%s: size=%zu, bucket_count=%zu, addr=%p", name, map.size(), map.bucket_count(), &map)

#endif
