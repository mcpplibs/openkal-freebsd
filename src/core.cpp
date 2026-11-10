// The interfaces every program reaches first: the version and the interfaces
// stated, ending, streams, the environment, time and entropy.
#include "common.h"

namespace okf {

int    g_argc = 0;
char** g_argv = nullptr;
char** g_envp = nullptr;

const char* variable(const char* name, unsigned long n) {
    for (char** e = g_envp; e && *e; ++e)
        if (same(*e, name, n) && (*e)[n] == '=') return *e + n + 1;
    return nullptr;
}

}  // namespace okf

namespace {

#ifndef OKF_STANDALONE
// THE PROGRAM'S ARGUMENTS, AS ITS INITIALISERS ARE GIVEN THEM (hosted). FreeBSD's
// run-time linker, and the start of a static program, call each function of
// .init_array with (argc, argv, envp): the program's own runtime received
// control. Standalone, this implementation's start records them (start.cpp),
// and the C library's start calls the initialisers with no arguments (musl's
// `libc_start_init'): a function reading three here read what the registers
// held, and the arguments it recorded over the start's were not pointers
// (the conformance suite stopped with SIGBUS in its first `kal_env_arg').
[[gnu::constructor(101)]] void record(int argc, char** argv, char** envp) {
    okf::g_argc = argc;
    okf::g_argv = argv;
    okf::g_envp = envp;
}
#endif

kal_u64 ns_of(int clock) {
    okf::ktimespec t{};
    okf::clock_gettime(clock, &t);
    return static_cast<kal_u64>(t.tv_sec) * 1000000000ull + static_cast<kal_u64>(t.tv_nsec);
}

}  // namespace

extern "C" {

// ================================================================ version
kal_u64 kal_version(void) { return KAL_VERSION; }
kal_u64 kal_interfaces(void) {
    return KAL_IFACE_ABORT | KAL_IFACE_STREAM | KAL_IFACE_MEMORY | KAL_IFACE_ENV
         | KAL_IFACE_TIME | KAL_IFACE_RANDOM | KAL_IFACE_FS | KAL_IFACE_PROCESS
         | KAL_IFACE_TASK | KAL_IFACE_EXEC | KAL_IFACE_TERMINAL | KAL_IFACE_TIMEOUT
         | KAL_IFACE_MAP | KAL_IFACE_REGION;
}

// ================================================================ abort
[[noreturn]] void kal_exit(int code) { okf::exit(code); }

[[noreturn]] void kal_abort(const char* msg, kal_uintptr len) {
    if (msg && len) { okf::write(2, msg, len); okf::write(2, "\n", 1); }
    // SIGABRT, at its default disposition and not blocked, sent to this program;
    // a program that survived it ends with the status of an abort.
    okf::ksigaction dfl{};
    dfl.sa_handler = SIG_DFL;
    okf::sigaction(SIGABRT, &dfl, nullptr);
    okf::ksigset only{}; only.__bits[(SIGABRT - 1) / 32] = 1u << ((SIGABRT - 1) % 32);
    okf::sigprocmask(SIG_UNBLOCK, &only, nullptr);
    okf::kill(static_cast<int>(okf::getpid()), SIGABRT);
    okf::exit(134);
}

// ================================================================ stream
kal_stream kal_stdin (void) { return kal_stream{ okf::pack(0) }; }
kal_stream kal_stdout(void) { return kal_stream{ okf::pack(1) }; }
kal_stream kal_stderr(void) { return kal_stream{ okf::pack(2) }; }

kal_intptr kal_stream_write(kal_stream s, const void* buf, kal_uintptr len) {
    const int fd = okf::unpack(s.h);
    if (fd < 0) return -kal_err_invalid;
    const char* p = static_cast<const char*>(buf);
    kal_uintptr done = 0;
    while (done < len) {
        const okf::word r = okf::write(fd, p + done, len - done);
        if (r == -EINTR) continue;
        if (okf::failed(r)) return done ? static_cast<kal_intptr>(done) : okf::nfail(r);
        done += static_cast<kal_uintptr>(r);
    }
    return static_cast<kal_intptr>(done);
}
kal_intptr kal_stream_read(kal_stream s, void* buf, kal_uintptr len) {
    const int fd = okf::unpack(s.h);
    if (fd < 0) return -kal_err_invalid;
    for (;;) {
        const okf::word r = okf::read(fd, buf, len);
        if (r == -EINTR) continue;
        return okf::failed(r) ? okf::nfail(r) : static_cast<kal_intptr>(r);
    }
}
int kal_stream_flush(kal_stream s) { return okf::unpack(s.h) < 0 ? kal_err_invalid : kal_ok; }
kal_uintptr kal_stream_props(kal_stream s) {
    const int fd = okf::unpack(s.h);
    okf::ktermios t{};
    return (fd >= 0 && !okf::failed(okf::ioctl(fd, TIOCGETA, &t))) ? KAL_STREAM_PROP_INTERACTIVE : 0;
}

// ================================================================ env
kal_uintptr kal_env_arg_count(void) { return static_cast<kal_uintptr>(okf::g_argc); }
kal_intptr kal_env_arg(kal_uintptr i, char* out, kal_uintptr cap) {
    if (i >= static_cast<kal_uintptr>(okf::g_argc)) return -kal_err_invalid;
    const char* a = okf::g_argv[i] ? okf::g_argv[i] : "";
    return okf::put(a, okf::length(a), out, cap);
}
kal_intptr kal_env_var(const char* name, kal_uintptr n, char* out, kal_uintptr cap) {
    if (!name) return -kal_err_invalid;
    const char* v = okf::variable(name, n);
    return v ? okf::put(v, okf::length(v), out, cap) : -kal_err_not_found;
}
kal_uintptr kal_env_var_count(void) {
    kal_uintptr n = 0;
    for (char** e = okf::g_envp; e && *e; ++e) ++n;
    return n;
}
kal_intptr kal_env_var_at(kal_uintptr i, char* out, kal_uintptr cap) {
    char** e = okf::g_envp;
    for (kal_uintptr k = 0; e && e[k]; ++k) {
        if (k != i) continue;
        unsigned long n = 0;
        while (e[k][n] && e[k][n] != '=') ++n;
        return okf::put(e[k], n, out, cap);
    }
    return -kal_err_invalid;
}
// The image's name as the kernel recorded it (kern.proc.pathname).
kal_intptr kal_env_image(char* out, kal_uintptr cap) {
    const int name[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    char buf[okf::kPath];
    unsigned long n = sizeof buf;
    if (okf::failed(okf::sysctl(name, 4, buf, &n)) || n == 0) return -kal_err_not_found;
    if (buf[n - 1] == 0) --n;
    return okf::put(buf, n, out, cap);
}
kal_intptr kal_env_loader(char*, kal_uintptr) { return -kal_err_not_found; }
kal_intptr kal_env_system(char* out, kal_uintptr cap) { return okf::put("freebsd", 7, out, cap); }
// THE RESOURCE PREFERENCE (0.23), which a start conveys in KAL_RESOURCES, as
// the other implementations convey it.
int kal_env_resources(void) {
    static int known = -1;
    int v = __atomic_load_n(&known, __ATOMIC_RELAXED);
    if (v >= 0) return v;
    v = KAL_RESOURCES_BALANCED;
    if (const char* r = okf::variable("KAL_RESOURCES", 13)) {
        const unsigned long n = okf::length(r);
        if (n == 11 && okf::same(r, "performance", 11)) v = KAL_RESOURCES_PERFORMANCE;
        else if (n == 7 && okf::same(r, "economy", 7)) v = KAL_RESOURCES_ECONOMY;
    }
    __atomic_store_n(&known, v, __ATOMIC_RELAXED);
    return v;
}

// ================================================================ time
kal_duration kal_time_monotonic(void) { return ns_of(CLOCK_MONOTONIC); }
kal_duration kal_time_wall(void) { return ns_of(CLOCK_REALTIME); }
kal_duration kal_time_monotonic_granularity(void) {
    okf::ktimespec t{};
    if (okf::failed(okf::clock_getres(CLOCK_MONOTONIC, &t))) return 1;
    const kal_u64 g = static_cast<kal_u64>(t.tv_sec) * 1000000000ull + static_cast<kal_u64>(t.tv_nsec);
    return g ? g : 1;
}
void kal_time_sleep(kal_duration ns) {
    okf::ktimespec t{ static_cast<long long>(ns / 1000000000ull), static_cast<long>(ns % 1000000000ull) };
    while (okf::nanosleep(&t, &t) == -EINTR) {}
}
kal_uintptr kal_time_props(void) { return KAL_TIME_PROP_WALL_AVAILABLE | KAL_TIME_PROP_SLEEP_PRECISE; }

// ================================================================ random
int kal_random_fill(void* out, kal_uintptr len) {
    auto* p = static_cast<unsigned char*>(out);
    while (len) {
        const okf::word r = okf::getrandom(p, len > 256 ? 256 : len, 0);
        if (r == -EINTR) continue;
        if (okf::failed(r) || r == 0) return kal_err_io;
        p += r; len -= static_cast<kal_uintptr>(r);
    }
    return kal_ok;
}
kal_uintptr kal_random_props(void) { return 0; }

}  // extern "C"
