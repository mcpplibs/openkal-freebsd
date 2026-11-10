// What the files of this implementation share: handles, the kernel's error
// numbers in openkal's words, names as the kernel takes them, and the program's
// arguments and environment.
#pragma once

#include "sys.h"
#include <openkal.h>

namespace okf {

// A HANDLE IS A DESCRIPTOR PLUS ONE, so that zero is never a valid handle.
inline kal_uintptr pack(int fd) { return fd < 0 ? 0 : static_cast<kal_uintptr>(fd) + 1u; }
inline int unpack(kal_uintptr h) { return h == 0 ? -1 : static_cast<int>(h - 1u); }

// The kernel's error number (positive), in openkal's words.
inline int translate(word e) {
    switch (e) {
        case 0:            return kal_ok;
        case EAGAIN:       return kal_err_again;
        case ENOMEM:       return kal_err_no_memory;
        case ENOSPC:
        case EDQUOT:       return kal_err_no_space;
        case EACCES:
        case EPERM:
        case EROFS:        return kal_err_permission;
        case ENOSYS:
        case EOPNOTSUPP:   return kal_err_not_supported;
        case EPIPE:
        case ECONNRESET:   return kal_err_closed;
        case ENOENT:       return kal_err_not_found;
        case EEXIST:       return kal_err_exists;
        case ENOTEMPTY:    return kal_err_not_empty;
        case EISDIR:       return kal_err_is_directory;
        case ENOTDIR:      return kal_err_not_directory;
        case ENOEXEC:      return kal_err_not_program;
        case EBADF:
        case EINVAL:
        case ENAMETOOLONG:
        case ELOOP:
        case EFAULT:       return kal_err_invalid;
        default:           return kal_err_io;
    }
}
// A system call's answer, when it failed, in openkal's words.
inline int fail(word r) { return translate(-r); }
inline kal_intptr nfail(word r) { return -static_cast<kal_intptr>(translate(-r)); }

inline unsigned long length(const char* s) { unsigned long n = 0; while (s && s[n]) ++n; return n; }
inline void copy(void* to, const void* from, unsigned long n) {
    auto* d = static_cast<char*>(to); auto* s = static_cast<const char*>(from);
    for (unsigned long i = 0; i < n; ++i) d[i] = s[i];
}
inline void zero(void* to, unsigned long n) { auto* d = static_cast<char*>(to); for (unsigned long i = 0; i < n; ++i) d[i] = 0; }
inline bool same(const char* a, const char* b, unsigned long n) { for (unsigned long i = 0; i < n; ++i) if (a[i] != b[i]) return false; return true; }

// What an operation that answers a length writes: as much as fits, and the
// whole length.
inline kal_intptr put(const char* v, kal_uintptr n, char* out, kal_uintptr cap) {
    if (out && cap) copy(out, v, n < cap ? n : cap);
    return static_cast<kal_intptr>(n);
}

constexpr unsigned long kPath = 1024;   // the kernel's MAXPATHLEN

// A name of openkal's (bytes and a length) as the kernel takes it: terminated,
// relative, with no component that ascends and no zero byte within.
struct cname {
    char buf[kPath];
    bool ok = false;
    cname(const char* s, kal_uintptr n) {
        if (s == nullptr || n == 0 || n >= sizeof buf) return;
        if (s[0] == '/') return;
        for (kal_uintptr i = 0; i < n; ++i) { if (s[i] == 0) return; buf[i] = s[i]; }
        buf[n] = 0;
        for (kal_uintptr i = 0; i < n; ) {
            kal_uintptr j = i;
            while (j < n && buf[j] != '/') ++j;
            if (j - i == 2 && buf[i] == '.' && buf[i + 1] == '.') return;
            i = j + 1;
        }
        ok = true;
    }
};

// The program's arguments and environment, as its initialisers were given them.
extern int    g_argc;
extern char** g_argv;
extern char** g_envp;

// A value of the environment, or null.
const char* variable(const char* name, unsigned long n);

// A lock of one word, for the few tables this implementation keeps; the wait is
// the kernel's (sched_yield), never the C library's.
struct spin {
    volatile int held = 0;
    void lock() { while (__atomic_exchange_n(&held, 1, __ATOMIC_ACQUIRE)) okf::sched_yield(); }
    void unlock() { __atomic_store_n(&held, 0, __ATOMIC_RELEASE); }
};

// openkal.process's own, for openkal.timeout: the identifier of a program this
// one started, and its status when it has ended (`done').
int reap_program(int pid, int opts, int* status, int* terminated, bool* done);
int started_pid(kal_process p, int* pid);

}  // namespace okf
