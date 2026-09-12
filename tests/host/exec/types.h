/* Amiga types, so the system-independent code can be built on the host. */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H

#include <stdint.h>

typedef uint8_t UBYTE;
typedef int16_t WORD;
typedef uint16_t UWORD;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int16_t BOOL;

#define TRUE 1
#define FALSE 0

#endif
