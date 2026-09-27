/*
 * armdos/cxxrt.cpp - the bits of the C++ runtime EDIT needs, without
 * exceptions: operator new/delete on malloc/free (a failed new returns
 * NULL, which Turbo Vision checks for - it never relied on bad_alloc), a
 * pure-virtual trap, and the std::__throw_* helpers libstdc++'s containers
 * call (they would otherwise drag in the whole exception machinery).
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include <stdlib.h>
#include <stddef.h>
#include <new>
#include <unistd.h>

static void fatal(const char *msg)
{
    size_t n = 0;
    while (msg[n])
        ++n;
    write(2, msg, n);
    _exit(3);
}

void *operator new(size_t n) { return malloc(n ? n : 1); }
void *operator new[](size_t n) { return malloc(n ? n : 1); }
void *operator new(size_t n, const std::nothrow_t &) noexcept { return malloc(n ? n : 1); }
void *operator new[](size_t n, const std::nothrow_t &) noexcept { return malloc(n ? n : 1); }
void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
void operator delete[](void *p, size_t) noexcept { free(p); }

extern "C" void __cxa_pure_virtual() { fatal("Pure virtual function called\r\n"); }

namespace std
{
    void __throw_bad_alloc() { fatal("Out of memory\r\n"); }
    void __throw_bad_array_new_length() { fatal("Out of memory\r\n"); }
    void __throw_length_error(const char *) { fatal("Out of memory\r\n"); }
    void __throw_out_of_range(const char *) { fatal("Internal error (range)\r\n"); }
    void __throw_out_of_range_fmt(const char *, ...) { fatal("Internal error (range)\r\n"); }
    void __throw_logic_error(const char *) { fatal("Internal error (logic)\r\n"); }
    void __throw_invalid_argument(const char *) { fatal("Internal error (argument)\r\n"); }
    void __throw_bad_function_call() { fatal("Internal error (function)\r\n"); }
}

