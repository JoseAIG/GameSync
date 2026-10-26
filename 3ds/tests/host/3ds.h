// Minimal stand-in for libctru's <3ds.h> so plain-C client modules that only
// need its integer types (through include/common.h) build with a host gcc.
// Host tests only; never on the include path of the real build.
#ifndef HOST_STUB_3DS_H
#define HOST_STUB_3DS_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef enum {
    MEDIATYPE_NAND = 0,
    MEDIATYPE_SD = 1,
    MEDIATYPE_GAME_CARD = 2,
} FS_MediaType;

#endif
