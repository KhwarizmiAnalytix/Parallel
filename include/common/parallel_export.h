/*
 * XSigma DLL Export/Import Header
 *
 * This file defines macros for symbol visibility control across different
 * platforms and build configurations (static vs shared libraries).
 *
 * Usage:
 * - PARALLEL_API: functions and methods to export/import across a DLL boundary
 * - PARALLEL_VISIBILITY: class declarations that need matching visibility (non-Windows)
 *
 * CMake compile definitions:
 * - PARALLEL_STATIC_DEFINE — static library, no decoration
 * - PARALLEL_SHARED_DEFINE — shared library
 * - PARALLEL_BUILDING_DLL — defined when compiling the Parallel DLL (Windows)
 */

#pragma once

#if defined(PARALLEL_STATIC_DEFINE)
#define PARALLEL_API
#define PARALLEL_VISIBILITY

#elif defined(PARALLEL_SHARED_DEFINE)
#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef PARALLEL_BUILDING_DLL
#define PARALLEL_API __declspec(dllexport)
#else
#define PARALLEL_API __declspec(dllimport)
#endif
#define PARALLEL_VISIBILITY
#elif defined(__GNUC__) && __GNUC__ >= 4
#define PARALLEL_API __attribute__((visibility("default")))
#define PARALLEL_VISIBILITY __attribute__((visibility("default")))
#else
#define PARALLEL_API
#define PARALLEL_VISIBILITY
#endif

#else
#define PARALLEL_API
#define PARALLEL_VISIBILITY
#endif
