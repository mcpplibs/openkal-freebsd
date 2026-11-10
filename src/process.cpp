// openkal.process: a program started by fork and fexecve, awaited by wait4,
// units as process groups, and the requests to end taken over by sigaction.
#include "common.h"

namespace {

using okf::word;

// HANDLES. A program this one started is its identifier plus one; a program
// found by its identifier carries kOpened, and this program itself kSelf, so
// that the operations a handle admits are told apart: a program does not wait
// for itself, and a program found by its identifier is not the caller's to end.
constexpr kal_uintptr kOpened = kal_uintptr(1) << 63;
constexpr kal_uintptr kSelf   = kal_uintptr(1) << 62;
kal_uintptr started_handle(int pid) { return static_cast<kal_uintptr>(pid) + 1u; }
int pid_of(kal_process p) { return static_cast<int>((p.h & ~(kOpened | kSelf)) - 1u); }

// WHAT AN AWAITED PROGRAM REPORTED, kept while its handle is held: a second
// wait answers the same, and its processor time remains (0.21).
struct ended_program { int pid; int status; int terminated; kal_u64 cpu_ns; };
constexpr int kEndedMax = 256;
ended_program g_ended[kEndedMax];
int g_ended_count = 0;
okf::spin g_ended_lock;

bool ended_record(int pid, ended_program* out) {
    g_ended_lock.lock();
    bool found = false;
    for (int i = 0; i < g_ended_count; ++i)
        if (g_ended[i].pid == pid) { if (out) *out = g_ended[i]; found = true; break; }
    g_ended_lock.unlock();
    return found;
}
void ended_forget(int pid) {
    g_ended_lock.lock();
    for (int i = 0; i < g_ended_count; ++i)
        if (g_ended[i].pid == pid) { g_ended[i] = g_ended[--g_ended_count]; break; }
    g_ended_lock.unlock();
}

kal_u64 cpu_of(const struct rusage& ru) {
    return (static_cast<kal_u64>(ru.ru_utime.tv_sec) + static_cast<kal_u64>(ru.ru_stime.tv_sec)) * 1000000000ull
         + (static_cast<kal_u64>(ru.ru_utime.tv_usec) + static_cast<kal_u64>(ru.ru_stime.tv_usec)) * 1000ull;
}

int reap(int pid, int opts, int* status, int* terminated, bool* done) {
    ended_program e{};
    if (!ended_record(pid, &e)) {
        int s = 0; struct rusage ru{}; word r;
        do r = okf::wait4(pid, &s, opts, &ru); while (r == -EINTR);
        if (okf::failed(r)) return okf::fail(r);
        *done = r != 0;
        if (!*done) return kal_ok;
        e.pid = pid;
        if (WIFEXITED(s)) { e.status = WEXITSTATUS(s); e.terminated = 0; }
        else { e.status = WIFSIGNALED(s) ? 128 + WTERMSIG(s) : 1; e.terminated = 1; }
        e.cpu_ns = cpu_of(ru);
        g_ended_lock.lock();
        if (g_ended_count < kEndedMax) g_ended[g_ended_count++] = e;
        g_ended_lock.unlock();
    }
    *done = true;
    if (status) *status = e.status;
    if (terminated) *terminated = e.terminated;
    return kal_ok;
}

// The vectors a start hands the kernel, in memory of kal_alloc's.
char* copy_of(const char* s, kal_uintptr n) {
    auto* c = static_cast<char*>(kal_alloc(n + 1, 1));
    if (c) { okf::copy(c, s, n); c[n] = 0; }
    return c;
}

// ---------------------------------------------------------------- the stop word
// THE REQUESTS TO END, TAKEN OVER. SIGINT, SIGTERM and SIGHUP set the word,
// counting each and never zero again once one has arrived; a request the
// program's starter arranged to ignore stays ignored. The handler wakes the
// word's waiters itself: the wake is a system call, which a handler may make.
kal_u32 g_stop_word = 0;
int g_stop_signal = 0;
int g_stop_state = 0;   // 0: not armed, 1: arming, 2: armed
const int kStopSignals[] = { SIGINT, SIGTERM, SIGHUP };

void stop_handler(int signo) {
    int none = 0;
    __atomic_compare_exchange_n(&g_stop_signal, &none, signo, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    kal_u32 seen = __atomic_load_n(&g_stop_word, __ATOMIC_RELAXED), next;
    do {
        next = seen + 1u;
        if (next == 0) next = 1u;
    } while (!__atomic_compare_exchange_n(&g_stop_word, &seen, next, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
    okf::umtx(&g_stop_word, UMTX_OP_WAKE_PRIVATE, 0x7fffffff, nullptr, nullptr);
}

void arm_stop() {
    if (__atomic_load_n(&g_stop_state, __ATOMIC_ACQUIRE) == 2) return;
    int expected = 0;
    if (!__atomic_compare_exchange_n(&g_stop_state, &expected, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&g_stop_state, __ATOMIC_ACQUIRE) != 2) okf::sched_yield();
        return;
    }
    for (const int sig : kStopSignals) {
        struct sigaction old{};
        if (!okf::failed(okf::sigaction(sig, nullptr, &old)) && old.sa_handler == SIG_IGN) continue;
        struct sigaction act{};
        act.sa_handler = stop_handler;
        act.sa_flags = SA_RESTART;
        okf::sigaction(sig, &act, nullptr);
    }
    __atomic_store_n(&g_stop_state, 2, __ATOMIC_RELEASE);
}

}  // namespace

extern "C" {

kal_uintptr kal_process_props(void) {
    return KAL_PROCESS_PROP_TERMINATE | KAL_PROCESS_PROP_STREAM_PASSING | KAL_PROCESS_PROP_EXIT_STATUS
         | KAL_PROCESS_PROP_CHANNEL | KAL_PROCESS_PROP_JOB | KAL_PROCESS_PROP_NULL_STREAMS
         | KAL_PROCESS_PROP_OPEN_ID | KAL_PROCESS_PROP_STOP_REQUESTED;
}

int kal_process_spawn(const kal_spawn* how, const char* path, kal_uintptr path_len,
                      const char** argv, const kal_uintptr* argv_lens, kal_uintptr argc,
                      const char** envp, const kal_uintptr* envp_lens, kal_uintptr envc,
                      const kal_spawn_streams* streams, kal_process* out) {
    if (!how || !path || !out || path_len == 0 || path_len >= okf::kPath) return kal_err_invalid;
    if (how->grant_count) return kal_err_not_supported;
    const kal_uintptr unsupported = KAL_SPAWN_BOUND_LIFETIME | KAL_SPAWN_ONE_STRING | KAL_SPAWN_CONFINE;
    if (how->flags & unsupported) return kal_err_not_supported;
    // One preference at most, and the null device and a placed stream are not
    // both at one position.
    const kal_uintptr prefs = how->flags & (KAL_SPAWN_RESOURCES_BALANCED | KAL_SPAWN_RESOURCES_PERFORMANCE
                                            | KAL_SPAWN_RESOURCES_ECONOMY);
    if (prefs & (prefs - 1)) return kal_err_invalid;
    const kal_uintptr nulls[3] = { KAL_SPAWN_NULL_IN, KAL_SPAWN_NULL_OUT, KAL_SPAWN_NULL_ERR };
    if (streams) {
        const kal_uintptr placed[3] = { streams->in.h, streams->out.h, streams->err.h };
        for (int i = 0; i < 3; ++i) if (placed[i] && (how->flags & nulls[i])) return kal_err_invalid;
    }
    for (kal_uintptr i = 0; i < envc; ++i) {
        if (!envp || !envp[i] || !envp_lens) return kal_err_invalid;
        kal_uintptr eq = 0;
        while (eq < envp_lens[i] && envp[i][eq] != '=') ++eq;
        if (eq == 0 || eq == envp_lens[i]) return kal_err_invalid;
    }
    const int base = okf::unpack(how->base.h), work = okf::unpack(how->work.h);
    if (base < 0 || work < 0) return kal_err_invalid;

    // The program is named relative to `base': resolved to a descriptor now,
    // so that a start that cannot happen is reported by its reason.
    char p[okf::kPath]; okf::copy(p, path, path_len); p[path_len] = 0;
    const word exe = okf::openat(base, p, O_RDONLY | O_CLOEXEC);
    if (okf::failed(exe)) return okf::fail(exe);
    struct stat st{};
    const word sr = okf::fstat(static_cast<int>(exe), &st);
    if (okf::failed(sr)) { okf::close(static_cast<int>(exe)); return okf::fail(sr); }
    if (S_ISDIR(st.st_mode)) { okf::close(static_cast<int>(exe)); return kal_err_is_directory; }
    if (!(st.st_mode & 0111)) { okf::close(static_cast<int>(exe)); return kal_err_permission; }

    // A position with no stream placed and no null device keeps the caller's.
    int fds[3] = { -1, -1, -1 };
    if (streams) { fds[0] = okf::unpack(streams->in.h); fds[1] = okf::unpack(streams->out.h); fds[2] = okf::unpack(streams->err.h); }
    for (int i = 0; i < 3; ++i) {
        if (!(how->flags & nulls[i])) continue;
        const word n = okf::openat(AT_FDCWD, "/dev/null", (i == 0 ? O_RDONLY : O_WRONLY) | O_CLOEXEC);
        if (okf::failed(n)) {
            for (int k = 0; k < i; ++k) if (how->flags & nulls[k]) okf::close(fds[k]);
            okf::close(static_cast<int>(exe));
            return okf::fail(n);
        }
        fds[i] = static_cast<int>(n);
    }

    // THE VECTORS: the arguments, and the environment given or this program's,
    // with the preference in KAL_RESOURCES where the start states one.
    const char* pref = prefs == KAL_SPAWN_RESOURCES_PERFORMANCE ? "KAL_RESOURCES=performance"
                     : prefs == KAL_SPAWN_RESOURCES_ECONOMY     ? "KAL_RESOURCES=economy"
                     : prefs == KAL_SPAWN_RESOURCES_BALANCED    ? "KAL_RESOURCES=balanced" : nullptr;
    kal_uintptr inherited = 0;
    if (envc == 0 && !envp) for (char** e = okf::g_envp; e && *e; ++e) ++inherited;
    const kal_uintptr av_n = argc + 1, ev_n = envc + inherited + 2;
    auto** av = static_cast<char**>(kal_alloc(av_n * sizeof(char*), alignof(char*)));
    auto** ev = static_cast<char**>(kal_alloc(ev_n * sizeof(char*), alignof(char*)));
    kal_uintptr en = 0;
    bool whole = av && ev;
    if (av) okf::zero(av, av_n * sizeof(char*));
    if (ev) okf::zero(ev, ev_n * sizeof(char*));
    for (kal_uintptr i = 0; whole && i < argc; ++i) whole = (av[i] = copy_of(argv[i], argv_lens[i])) != nullptr;
    for (kal_uintptr i = 0; whole && i < envc; ++i) {
        if (pref && envp_lens[i] >= 14 && okf::same(envp[i], "KAL_RESOURCES=", 14)) continue;
        whole = (ev[en++] = copy_of(envp[i], envp_lens[i])) != nullptr;
    }
    for (kal_uintptr i = 0; whole && i < inherited; ++i) {
        const char* e = okf::g_envp[i];
        if (pref && okf::same(e, "KAL_RESOURCES=", 14)) continue;
        whole = (ev[en++] = copy_of(e, okf::length(e))) != nullptr;
    }
    if (whole && pref) whole = (ev[en++] = copy_of(pref, okf::length(pref))) != nullptr;
    auto release = [&] {
        if (av) { for (kal_uintptr i = 0; i < argc; ++i) if (av[i]) kal_free(av[i], argv_lens[i] + 1, 1); kal_free(av, av_n * sizeof(char*), alignof(char*)); }
        if (ev) { for (kal_uintptr i = 0; i < en; ++i) if (ev[i]) kal_free(ev[i], okf::length(ev[i]) + 1, 1); kal_free(ev, ev_n * sizeof(char*), alignof(char*)); }
        for (int i = 0; i < 3; ++i) if (how->flags & nulls[i]) okf::close(fds[i]);
        okf::close(static_cast<int>(exe));
    };
    if (!whole) { release(); return kal_err_no_memory; }

    int report[2];
    const word pr = okf::pipe2(report, O_CLOEXEC);
    if (okf::failed(pr)) { release(); return okf::fail(pr); }
    const int join = how->job ? static_cast<int>(how->job->h) : 0;   // 0: a new unit

    const word pid = okf::fork();
    if (pid == 0) {
        // THE COPY: system calls only, until the program replaces it.
        auto refused = [&](word r) {
            const int why = static_cast<int>(-r);
            okf::write(report[1], &why, sizeof why);
            okf::exit(127);
        };
        if (how->job) okf::setpgid(0, join);
        int saved[3] = { -1, -1, -1 };
        for (int i = 0; i < 3; ++i) {
            if (fds[i] < 0) continue;
            const word d = okf::fcntl(fds[i], F_DUPFD_CLOEXEC, 10);
            if (okf::failed(d)) refused(d);
            saved[i] = static_cast<int>(d);
        }
        for (int i = 0; i < 3; ++i) {
            if (saved[i] < 0) continue;
            const word d = okf::dup2(saved[i], i);
            if (okf::failed(d)) refused(d);
        }
        struct sigaction dfl{};
        dfl.sa_handler = SIG_DFL;
        for (int s = 1; s < NSIG; ++s) if (s != SIGKILL && s != SIGSTOP) okf::sigaction(s, &dfl, nullptr);
        sigset_t none{};
        okf::sigprocmask(SIG_SETMASK, &none, nullptr);
        const word c = okf::fchdir(work);
        if (okf::failed(c)) refused(c);
        refused(okf::fexecve(static_cast<int>(exe), av, ev));
    }
    okf::close(report[1]);
    release();
    if (okf::failed(pid)) { okf::close(report[0]); return okf::fail(pid); }
    if (how->job) okf::setpgid(static_cast<int>(pid), join);   // both sides, as shells do
    int e = 0; word r;
    do r = okf::read(report[0], &e, sizeof e); while (r == -EINTR);
    okf::close(report[0]);
    if (r == sizeof e) {
        int s = 0;
        okf::wait4(static_cast<int>(pid), &s, 0, nullptr);
        return okf::translate(e);
    }
    if (how->job && how->job->h == 0) how->job->h = static_cast<kal_uintptr>(pid);
    *out = kal_process{ started_handle(static_cast<int>(pid)) };
    return kal_ok;
}

int kal_process_channel(kal_stream* mine, kal_stream* theirs) {
    if (!mine || !theirs) return kal_err_invalid;
    int p[2];
    const word r = okf::pipe2(p, O_CLOEXEC);
    if (okf::failed(r)) return okf::fail(r);
    *mine = kal_stream{ okf::pack(p[0]) }; *theirs = kal_stream{ okf::pack(p[1]) };
    return kal_ok;
}
void kal_process_channel_close(kal_stream s) { const int fd = okf::unpack(s.h); if (fd >= 0) okf::close(fd); }

int kal_process_wait(kal_process p, int* status, int* terminated) {
    if (p.h == 0 || (p.h & kSelf)) return kal_err_invalid;
    if (p.h & kOpened) return kal_err_not_supported;
    bool done = false;
    return reap(pid_of(p), 0, status, terminated, &done);
}
// A REQUEST TO END: SIGTERM, which the program may take over.
int kal_process_terminate(kal_process p) {
    if (p.h == 0 || (p.h & kSelf)) return kal_err_invalid;
    if (p.h & kOpened) return kal_err_not_supported;
    if (ended_record(pid_of(p), nullptr)) return kal_ok;
    const word r = okf::kill(pid_of(p), SIGTERM);
    return okf::failed(r) ? okf::fail(r) : kal_ok;
}
void kal_process_close(kal_process p) {
    if (p.h != 0 && !(p.h & (kOpened | kSelf))) ended_forget(pid_of(p));
}
int kal_process_self(kal_process* out) {
    if (!out) return kal_err_invalid;
    *out = kal_process{ started_handle(static_cast<int>(okf::getpid())) | kSelf };
    return kal_ok;
}
int kal_process_open_id(kal_u64 id, kal_process* out) {
    if (!out || id == 0 || id > 0x7fffffffu) return kal_err_invalid;
    if (okf::kill(static_cast<int>(id), 0) == -ESRCH) return kal_err_not_found;
    *out = kal_process{ started_handle(static_cast<int>(id)) | kOpened };
    return kal_ok;
}
int kal_process_info(kal_process p, kal_u32 wanted, kal_program_info* out) {
    if (!out || out->self_size < 8 || p.h == 0) return kal_err_invalid;
    const kal_u32 cap = out->self_size;
    kal_program_info i{}; i.self_size = cap;
    const int pid = pid_of(p);
    i.present = KAL_PINFO_ID; i.id = static_cast<kal_u64>(pid);
    if (p.h & kSelf) {
        struct rusage ru{};
        if (!okf::failed(okf::getrusage(RUSAGE_SELF, &ru))) { i.cpu_ns = cpu_of(ru); i.present |= KAL_PINFO_CPU; }
        i.present |= KAL_PINFO_ENDED; i.ended = 0;
    } else if (!(p.h & kOpened)) {
        ended_program e{};
        i.present |= KAL_PINFO_ENDED;
        if (ended_record(pid, &e)) { i.ended = 1; i.cpu_ns = e.cpu_ns; i.present |= KAL_PINFO_CPU; }
        else i.ended = 0;
    }
    i.present &= wanted | KAL_PINFO_ID;
    struct { kal_u32 bit; unsigned long end; } const fields[] = {
        { KAL_PINFO_ID,    offsetof(kal_program_info, id) + 8 },
        { KAL_PINFO_CPU,   offsetof(kal_program_info, cpu_ns) + 8 },
        { KAL_PINFO_ENDED, offsetof(kal_program_info, ended) + 4 },
    };
    for (const auto& f : fields) if (cap < f.end) i.present &= ~f.bit;
    okf::copy(out, &i, cap < sizeof i ? cap : sizeof i);
    return kal_ok;
}

// UNITS ARE PROCESS GROUPS.
int kal_process_job_enter(kal_job* j) {
    if (!j) return kal_err_invalid;
    if (j->h == 0) {
        const word r = okf::setpgid(0, 0);
        if (okf::failed(r)) return okf::fail(r);
        j->h = static_cast<kal_uintptr>(okf::getpid());
        return kal_ok;
    }
    const word r = okf::setpgid(0, static_cast<int>(j->h));
    return okf::failed(r) ? okf::fail(r) : kal_ok;
}
int kal_process_job_terminate(kal_job j) {
    if (!j.h) return kal_err_invalid;
    const word r = okf::kill(-static_cast<int>(j.h), SIGKILL);
    return okf::failed(r) ? okf::fail(r) : kal_ok;
}
void kal_process_job_close(kal_job) {}
// Declared units, usage and limits are not claimed (KAL_PROCESS_PROP_JOB_OBSERVE,
// _JOB_USAGE, _JOB_LIMIT_*), and the operations report so (clause 6.2).
int kal_process_job_create(kal_job*, char*, kal_uintptr, kal_uintptr*) { return kal_err_not_supported; }
int kal_process_job_open(const char*, kal_uintptr, kal_job*) { return kal_err_not_supported; }
int kal_process_job_observe(kal_job, const char*, kal_uintptr, kal_u32*) { return kal_err_not_supported; }
int kal_process_job_retire(kal_job, const char*, kal_uintptr, const kal_program_identity*, kal_u32*) { return kal_err_not_supported; }
int kal_process_job_usage(kal_job, kal_job_usage*) { return kal_err_not_supported; }
int kal_process_job_limit(kal_job, kal_uintptr kind, kal_u64) {
    return kind == KAL_JOB_LIMIT_MEMORY ? kal_err_not_supported : kal_err_invalid;
}

const kal_u32* kal_process_stop_requested(void) {
    arm_stop();
    return &g_stop_word;
}
int kal_process_end_as_requested(void) {
    const int sig = __atomic_load_n(&g_stop_signal, __ATOMIC_ACQUIRE);
    if (sig == 0 || __atomic_load_n(&g_stop_word, __ATOMIC_ACQUIRE) == 0) return kal_err_invalid;
    // The request's own disposition, restored, and the request made again.
    struct sigaction act{};
    act.sa_handler = SIG_DFL;
    okf::sigaction(sig, &act, nullptr);
    sigset_t one{}; one.__bits[(sig - 1) / 32] = 1u << ((sig - 1) % 32);
    okf::sigprocmask(SIG_UNBLOCK, &one, nullptr);
    okf::kill(static_cast<int>(okf::getpid()), sig);
    return kal_err_invalid;
}

}  // extern "C"

// Used by openkal.timeout.
namespace okf {
int reap_program(int pid, int opts, int* status, int* terminated, bool* done) { return reap(pid, opts, status, terminated, done); }
int started_pid(kal_process p, int* pid) {
    if (p.h == 0 || (p.h & kSelf)) return kal_err_invalid;
    if (p.h & kOpened) return kal_err_not_supported;
    *pid = pid_of(p);
    return kal_ok;
}
}  // namespace okf
