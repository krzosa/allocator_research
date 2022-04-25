#include <stdio.h>
#include <stdint.h>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define function static
#define global   static
typedef uint64_t SizeU;
typedef int64_t  SizeI;
typedef uint8_t  U8;
typedef uint32_t U32;
typedef uint64_t U64;
typedef int8_t   S8;
typedef int32_t  S32;
typedef int64_t  S64;
typedef int8_t   B8;
typedef int32_t  B32;
typedef int64_t  B64;
#define kib(x) ((x)*1024llu)
#define mib(x) (kib(x)*1024llu)
#define gib(x) (mib(x)*1024llu)
#define assert(x) do{ if(!(x)) __debugbreak(); } while(0)
#define assert_msg(x,...) assert(x)
#define buff_cap(x) (sizeof((x))/sizeof((x)[0]))