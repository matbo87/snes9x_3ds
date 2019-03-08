
#ifndef _SAR_H_
#define _SAR_H_


#include "port.h"



#ifdef RIGHTSHIFT_IS_SAR
#define SAR(b, n) ((b)>>(n))
#else

static inline int8 SAR(const int8 b, const int n){
#ifndef RIGHTSHIFT_INT8_IS_SAR
    if(b<0) return (b>>n)|(-1<<(8-n));
#endif
    return b>>n;
}

static inline int16 SAR(const int16 b, const int n){
#ifndef RIGHTSHIFT_INT16_IS_SAR
    if(b<0) return (b>>n)|(-1<<(16-n));
#endif
    return b>>n;
}

static inline int32 SAR(const int32 b, const int n){
#ifndef RIGHTSHIFT_INT32_IS_SAR
    if(b<0) return (b>>n)|(-1<<(32-n));
#endif
    return b>>n;
}

static inline int64 SAR(const int64 b, const int n){
#ifndef RIGHTSHIFT_INT64_IS_SAR
    if(b<0) return (b>>n)|(-1<<(64-n));
#endif
    return b>>n;
}

#endif

#endif

