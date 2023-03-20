#pragma once


#if defined(WIN32) || defined(_MSC_VER)

#if defined(LIBBASE_IMPLEMENTATION)
#define LIBBASE_EXPORT __declspec(dllexport)
#else
#define LIBBASE_EXPORT __declspec(dllimport)
#endif  // defined(LIBBASE_IMPLEMENTATION)

#else  // defined(WIN32)
#if defined(LIBBASE_IMPLEMENTATION)
#define LIBBASE_EXPORT __attribute__((visibility("default")))
#else
#define LIBBASE_EXPORT
#endif  // defined(LIBBASE_IMPLEMENTATION)
#endif
