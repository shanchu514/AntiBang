// 让这个 mod 默认使用 LeviLamina 的内存分配器，
// 避免跨 DLL 的 new/delete 不匹配（官方模板也带这个文件）。
#define LL_MEMORY_OPERATORS

#include "ll/api/memory/MemoryOperators.h" // IWYU pragma: keep
