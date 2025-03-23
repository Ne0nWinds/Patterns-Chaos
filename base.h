#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _MSC_VER
	#define COMPILER_MSVC 
#elif defined(__clang__) || defined(__GNUC__)
	#define COMPILER_CLANG_GCC
#else
	#error "Compiler Not Supported"
#endif

#ifdef COMPILER_MSVC
    #define Break() __debugbreak()
#elif COMPILER_CLANG_GCC
    #define Break() __builtin_trap()
#endif

/* == Base Types == */
typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef char char8;

typedef float f32;
typedef double f64;

#define ArrayLen(Array) (sizeof(Array) / sizeof(*Array))

#ifdef DEBUG_MODE
#define Assert(Expression) {\
	if (!(Expression)) {\
		printf("Assertion failed: %s, file %s, line: %d", #Expression, __FILE__, __LINE__);\
		Break();\
	}\
}
#else
#define Assert(Expression)
#endif

template <typename T>
struct auto_defer {
	T Callback;
	constexpr auto_defer(const T &InCallback) : Callback(InCallback) {}
	constexpr ~auto_defer() {
		Callback();
	}
};

#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)
#define OnScopeExit(...) auto_defer CONCAT(_auto_defer_, __COUNTER__) = auto_defer([&]() { __VA_ARGS__; })

/* == Memory Allocation == */

#define KB(N) (N * 1024ULL)
#define MB(N) (KB(N) * 1024ULL)
#define GB(N) (MB(N) * 1024ULL)

struct memory_arena {
	void *Base;
	u32 Offset;
	u32 Capacity;
};

[[nodiscard]]
memory_arena CreateMemoryArena(u32 Capacity) {
	memory_arena Result = {};
	Result.Base = malloc(Capacity);
	Result.Offset = 0;
	Result.Capacity = Capacity;
	return Result;
}

static inline constexpr u32 RoundUpPowerOf2(u32 N, u32 Multiple) {
	u32 MultipleMinusOne = Multiple - 1;
	u32 Mask = ~MultipleMinusOne;
	u32 Result = (N + MultipleMinusOne) & Mask;
	return Result;
}
static inline constexpr u32 RoundUpPowerOf2(u64 N, u64 Multiple) {
	u64 MultipleMinusOne = Multiple - 1LL;
	u64 Mask = ~MultipleMinusOne;
	u64 Result = (N + MultipleMinusOne) & Mask;
	return Result;
}

// TODO: Chained Arenas
static inline void *Push(memory_arena *Arena, u32 Size, u32 Alignment = 16) {
	u8 *Result = (u8 *)Arena->Base + Arena->Offset;

	u32 AlignedOffset = RoundUpPowerOf2(Arena->Offset, Alignment);
	Assert(AlignedOffset + Size <= Arena->Capacity);
	Arena->Offset = AlignedOffset + Size;

	for (u32 i = 0; i < Size; ++i) {
		Result[i] = 0;
	}
	return (void *)Result;
}

#define PUSHSTRUCT_2(Arena, Struct) (Struct *)Push(Arena, sizeof(Struct))
#define PUSHSTRUCT_3(Arena, Struct, Count) (Struct *)Push(Arena, sizeof(Struct) * (Count))

#define GET_MACRO(_1, _2, _3, NAME, ...) NAME
#define PushStruct(...) GET_MACRO(__VA_ARGS__, PUSHSTRUCT_3, PUSHSTRUCT_2)(__VA_ARGS__)

#include <bit>
static inline void Pop(memory_arena *Arena, void *Ptr) {
	u64 OffsetPointer = std::bit_cast<u64>(((u8 *)Arena->Base + Arena->Offset));

	Assert(std::bit_cast<u64>(Ptr) >= std::bit_cast<u64>(Arena->Base));
	Assert(std::bit_cast<u64>(Ptr) < std::bit_cast<u64>(OffsetPointer));

	(void)OffsetPointer;
	Arena->Offset = (u32)(std::bit_cast<u64>(Ptr) - std::bit_cast<u64>(Arena->Base));
}

static inline void Reset(memory_arena *Arena) {
	Arena->Offset = 0;
}

struct v2 {
	f32 X, Y;
};
struct v3_packed {
	f32 X, Y, Z;
};
struct v4 {
	f32 X, Y, Z, W;
};
using v3 = v4;

struct v2i {
	s32 X, Y;
};
struct v3i_packed {
	s32 X, Y, Z;
};
struct v4i {
	s32 X, Y, Z, W;
};
using v3i = v4i;

template <typename T>
struct range {
	T *Data;
	u32 Length;
};

#define CreateRange(Array) { Array, ArrayLen(Array) }

static memory_arena Temp;
