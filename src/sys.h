// The system call interface of FreeBSD, as this implementation issues it.
//
// EVERY OPERATION BELOW IS A SYSTEM CALL, issued here; none is a function of the
// C library. The system's headers are read for what they define --- numbers,
// flags and the layout of the structures the kernel reads and writes, which are
// the kernel's ABI --- and for nothing they would call.
//
// THE KERNEL'S ANSWER, AS ONE VALUE. FreeBSD reports a failure on amd64 by
// setting the carry flag, with the error number in %rax; a success leaves the
// carry clear and the result in %rax, and a second result, where there is one
// (fork), in %rdx. `call' folds the first two into one value: the result, or
// the error number negated, as the Linux implementation's are.
#pragma once

#include <errno.h>
#include <stddef.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/umtx.h>
#include <sys/wait.h>
#include <dirent.h>
#include <termios.h>
#include <sys/ioccom.h>
#include <sys/ttycom.h>
#include <unistd.h>   // SEEK_SET and its companions

#if !defined(__x86_64__)
#error "openkal-freebsd 0.1 issues the system calls of amd64 only"
#endif

namespace okf {

using word = long;

struct answer { word value; word second; };

inline answer call6(word n, word a = 0, word b = 0, word c = 0, word d = 0, word e = 0, word f = 0) {
    register word r10 __asm__("r10") = d;
    register word r8  __asm__("r8")  = e;
    register word r9  __asm__("r9")  = f;
    word rax = n, rdx = c;
    unsigned char failed;
    __asm__ __volatile__("syscall; setc %2"
                         : "+a"(rax), "+d"(rdx), "=r"(failed), "+r"(r10), "+r"(r8), "+r"(r9)
                         : "D"(a), "S"(b)
                         : "rcx", "r11", "memory", "cc");
    return answer{ failed ? -rax : rax, rdx };
}

inline word sys(word n, word a = 0, word b = 0, word c = 0, word d = 0, word e = 0, word f = 0) {
    return call6(n, a, b, c, d, e, f).value;
}

template <class T> inline word w(T v) { return (word)(v); }

// A value that is an error number, negated.
inline bool failed(word r) { return r < 0 && r > -4096; }

// The operations, by the names the system gives them.
inline word read(int fd, void* b, unsigned long n)        { return sys(SYS_read, fd, w(b), w(n)); }
inline word write(int fd, const void* b, unsigned long n) { return sys(SYS_write, fd, w(b), w(n)); }
inline word close(int fd)                                  { return sys(SYS_close, fd); }
inline word openat(int dir, const char* p, int flags, int mode = 0) { return sys(SYS_openat, dir, w(p), flags, mode); }
inline word fstat(int fd, struct stat* st)                 { return sys(SYS_fstat, fd, w(st)); }
inline word fstatat(int dir, const char* p, struct stat* st, int flags) { return sys(SYS_fstatat, dir, w(p), w(st), flags); }
inline word mkdirat(int dir, const char* p, int mode)      { return sys(SYS_mkdirat, dir, w(p), mode); }
inline word unlinkat(int dir, const char* p, int flags)    { return sys(SYS_unlinkat, dir, w(p), flags); }
inline word renameat(int a, const char* x, int b, const char* y) { return sys(SYS_renameat, a, w(x), b, w(y)); }
inline word linkat(int a, const char* x, int b, const char* y, int flags) { return sys(SYS_linkat, a, w(x), b, w(y), flags); }
inline word symlinkat(const char* t, int dir, const char* p) { return sys(SYS_symlinkat, w(t), dir, w(p)); }
inline word readlinkat(int dir, const char* p, char* b, unsigned long n) { return sys(SYS_readlinkat, dir, w(p), w(b), w(n)); }
inline word utimensat(int dir, const char* p, const struct timespec* t, int flags) { return sys(SYS_utimensat, dir, w(p), w(t), flags); }
inline word futimens(int fd, const struct timespec* t)     { return sys(SYS_futimens, fd, w(t)); }
inline word fchmodat(int dir, const char* p, int mode, int flags) { return sys(SYS_fchmodat, dir, w(p), mode, flags); }
inline word fcntl(int fd, int cmd, word arg = 0)           { return sys(SYS_fcntl, fd, cmd, arg); }
inline word ftruncate(int fd, long long n)                 { return sys(SYS_ftruncate, fd, w(n)); }
inline word lseek(int fd, long long off, int whence)       { return sys(SYS_lseek, fd, w(off), whence); }
inline word pread(int fd, void* b, unsigned long n, long long off) { return sys(SYS_pread, fd, w(b), w(n), w(off)); }
inline word pwrite(int fd, const void* b, unsigned long n, long long off) { return sys(SYS_pwrite, fd, w(b), w(n), w(off)); }
inline word fstatfs(int fd, struct statfs* s)              { return sys(SYS_fstatfs, fd, w(s)); }
inline word getdirentries(int fd, char* b, unsigned long n, off_t* base) { return sys(SYS_getdirentries, fd, w(b), w(n), w(base)); }
inline word mmap(void* a, unsigned long n, int prot, int flags, int fd, long long off) { return sys(SYS_mmap, w(a), w(n), prot, flags, fd, w(off)); }
inline word munmap(void* a, unsigned long n)               { return sys(SYS_munmap, w(a), w(n)); }
inline word mprotect(void* a, unsigned long n, int prot)   { return sys(SYS_mprotect, w(a), w(n), prot); }
inline word madvise(void* a, unsigned long n, int how)     { return sys(SYS_madvise, w(a), w(n), how); }
inline word msync(void* a, unsigned long n, int flags)     { return sys(SYS_msync, w(a), w(n), flags); }
inline word getpid()                                       { return sys(SYS_getpid); }
inline word kill(int pid, int sig)                         { return sys(SYS_kill, pid, sig); }
inline word wait4(int pid, int* status, int options, struct rusage* ru) { return sys(SYS_wait4, pid, w(status), options, w(ru)); }
inline word execve(const char* p, char* const* av, char* const* ev) { return sys(SYS_execve, w(p), w(av), w(ev)); }
inline word fexecve(int fd, char* const* av, char* const* ev) { return sys(SYS_fexecve, fd, w(av), w(ev)); }
inline word fchdir(int fd)                                 { return sys(SYS_fchdir, fd); }
inline word dup2(int from, int to)                         { return sys(SYS_dup2, from, to); }
inline word pipe2(int fds[2], int flags)                   { return sys(SYS_pipe2, w(fds), flags); }
inline word sigaction(int sig, const struct sigaction* a, struct sigaction* old) { return sys(SYS_sigaction, sig, w(a), w(old)); }
inline word sigprocmask(int how, const sigset_t* s, sigset_t* old) { return sys(SYS_sigprocmask, how, w(s), w(old)); }
inline word nanosleep(const struct timespec* t, struct timespec* rest) { return sys(SYS_nanosleep, w(t), w(rest)); }
inline word clock_gettime(int clock, struct timespec* t)   { return sys(SYS_clock_gettime, clock, w(t)); }
inline word clock_getres(int clock, struct timespec* t)    { return sys(SYS_clock_getres, clock, w(t)); }
inline word getrandom(void* b, unsigned long n, unsigned flags) { return sys(SYS_getrandom, w(b), w(n), flags); }
inline word getrusage(int who, struct rusage* ru)          { return sys(SYS_getrusage, who, w(ru)); }
inline word setpriority(int which, int who, int prio)      { return sys(SYS_setpriority, which, who, prio); }
inline word sched_yield()                                  { return sys(SYS_sched_yield); }
inline word ppoll(struct pollfd* p, unsigned n, const struct timespec* t, const sigset_t* m) { return sys(SYS_ppoll, w(p), w(n), w(t), w(m)); }
inline word ioctl(int fd, unsigned long req, void* arg)    { return sys(SYS_ioctl, fd, w(req), w(arg)); }
inline word umtx(void* obj, int op, unsigned long val, void* a1, void* a2) { return sys(SYS__umtx_op, w(obj), op, w(val), w(a1), w(a2)); }
inline word flock(int fd, int how)                         { return sys(SYS_flock, fd, how); }
inline word getcwd(char* b, unsigned long n)               { return sys(SYS___getcwd, w(b), w(n)); }
inline word setpgid(int pid, int pgid)                     { return sys(SYS_setpgid, pid, pgid); }
inline word sysctl(const int* name, unsigned n, void* old, unsigned long* oldlen, const void* nw = nullptr, unsigned long nwlen = 0) {
    return sys(SYS___sysctl, w(name), n, w(old), w(oldlen), w(nw), w(nwlen));
}
inline word sysctlbyname(const char* name, void* old, unsigned long* oldlen) {
    unsigned long n = 0; while (name[n]) ++n;
    return sys(SYS___sysctlbyname, w(name), w(n), w(old), w(oldlen), 0, 0);
}
[[noreturn]] inline void exit(int code) { for (;;) sys(SYS_exit, code); }

// fork's two answers: the child is told by the second (%rdx is 1 there), and
// its first is then the parent's identifier, not zero.
inline word fork() {
    const answer a = call6(SYS_fork);
    if (failed(a.value)) return a.value;
    return a.second ? 0 : a.value;
}

}  // namespace okf
