// The system call interface of FreeBSD, as this implementation issues it.
//
// EVERY OPERATION BELOW IS A SYSTEM CALL, issued here; none is a function of the
// C library, and no header of a C library is read. The numbers, flags and
// structure layouts are the kernel's ABI, stated below as FreeBSD 14 states
// them in its headers. They are identical on amd64 and arm64, and were read
// from the base system's headers by compiling them for both (each structure's
// size is asserted). An implementation that is the whole of a program's
// environment, or that lies beneath a C library written above openkal, cannot
// take them from a C library's headers: those may describe another kernel.
//
// THE KERNEL'S ANSWER, AS ONE VALUE. FreeBSD reports a failure by setting the
// carry flag, with the error number in the first result register (%rax, x0); a
// success leaves the carry clear and the result there, and a second result,
// where there is one (fork, pdfork), in the next (%rdx, x1). `call6' folds the
// first two into one value: the result, or the error number negated, as the
// Linux implementation's system calls answer.
#pragma once

using kal_ssize = long;

namespace okf {

using word = long;
using u8 = unsigned char; using u16 = unsigned short; using u32 = unsigned; using u64 = unsigned long long;
using i16 = short; using i32 = int; using i64 = long long;

namespace abi {
constexpr int AT_FDCWD = -100;
constexpr int AT_REMOVEDIR = 2048;
constexpr int AT_SYMLINK_NOFOLLOW = 512;
constexpr int CLOCK_MONOTONIC = 4;
constexpr int CLOCK_REALTIME = 0;
constexpr int CTL_KERN = 1;
constexpr int EACCES = 13;
constexpr int EAGAIN = 35;
constexpr int EBADF = 9;
constexpr int ECONNRESET = 54;
constexpr int EDQUOT = 69;
constexpr int EEXIST = 17;
constexpr int EFAULT = 14;
constexpr int EINTR = 4;
constexpr int EINVAL = 22;
constexpr int EISDIR = 21;
constexpr int ELOOP = 62;
constexpr int ENAMETOOLONG = 63;
constexpr int ENODEV = 19;
constexpr int ENOENT = 2;
constexpr int ENOEXEC = 8;
constexpr int ENOMEM = 12;
constexpr int ENOSPC = 28;
constexpr int ENOSYS = 78;
constexpr int ENOTDIR = 20;
constexpr int ENOTEMPTY = 66;
constexpr int ENOTTY = 25;
constexpr int EOPNOTSUPP = 45;
constexpr int EPERM = 1;
constexpr int EPIPE = 32;
constexpr int EROFS = 30;
constexpr int ESRCH = 3;
constexpr int ETIMEDOUT = 60;
constexpr int EWOULDBLOCK = 35;
constexpr int EXDEV = 18;
constexpr int ECHILD = 10;
constexpr int F_DUPFD_CLOEXEC = 17;
constexpr int F_RDLCK = 1;
constexpr int F_SETLK = 12;
constexpr int F_SETLKW = 13;
constexpr int F_UNLCK = 2;
constexpr int F_WRLCK = 3;
constexpr int F_GETFD = 1;
constexpr int F_SETFD = 2;
constexpr int FD_CLOEXEC = 1;
constexpr int ICANON = 256;
constexpr int ICRNL = 256;
constexpr int IEXTEN = 1024;
constexpr int IGNCR = 128;
constexpr int INLCR = 64;
constexpr int ISIG = 128;
constexpr int ISTRIP = 32;
constexpr int IXON = 512;
constexpr int ECHO = 8;
constexpr int VMIN = 16;
constexpr int VTIME = 17;
constexpr int KERN_PROC = 14;
constexpr int KERN_PROC_PATHNAME = 12;
constexpr int LOCK_EX = 2;
constexpr int LOCK_NB = 4;
constexpr int LOCK_SH = 1;
constexpr int LOCK_UN = 8;
constexpr int MADV_FREE = 5;
constexpr int MADV_WILLNEED = 3;
constexpr int MAP_ANON = 4096;
constexpr int MAP_EXCL = 16384;
constexpr int MAP_FIXED = 16;
constexpr int MAP_PRIVATE = 2;
constexpr int MAP_SHARED = 1;
constexpr int MAP_STACK = 1024;
constexpr int MS_SYNC = 0;
constexpr int O_APPEND = 8;
constexpr int O_CLOEXEC = 1048576;
constexpr int O_CREAT = 512;
constexpr int O_DIRECTORY = 131072;
constexpr int O_EXCL = 2048;
constexpr int O_RDONLY = 0;
constexpr int O_RDWR = 2;
constexpr int O_TRUNC = 1024;
constexpr int O_WRONLY = 1;
constexpr int O_NOFOLLOW = 256;
constexpr int POLLIN = 1;
constexpr int POLLOUT = 4;
constexpr int PRIO_PROCESS = 0;
constexpr int PROT_EXEC = 4;
constexpr int PROT_NONE = 0;
constexpr int PROT_READ = 1;
constexpr int PROT_WRITE = 2;
constexpr int RUSAGE_SELF = 0;
constexpr int SA_RESTART = 2;
constexpr int SEEK_CUR = 1;
constexpr int SEEK_END = 2;
constexpr int SEEK_SET = 0;
constexpr int SIGABRT = 6;
constexpr int SIGHUP = 1;
constexpr int SIGINT = 2;
constexpr int SIGKILL = 9;
constexpr int SIGSTOP = 17;
constexpr int SIGTERM = 15;
constexpr int SIGPIPE = 13;
constexpr int SIG_SETMASK = 3;
constexpr int SIG_UNBLOCK = 2;
constexpr int SIG_BLOCK = 1;
constexpr int NSIG = 32;
constexpr unsigned long TIOCGETA = 1076655123ul;
constexpr unsigned long TIOCGWINSZ = 1074295912ul;
constexpr unsigned long TIOCSETAF = 2150396950ul;
constexpr int UMTX_OP_WAIT = 2;
constexpr int UMTX_OP_WAIT_UINT_PRIVATE = 15;
constexpr int UMTX_OP_WAKE_PRIVATE = 16;
constexpr int UMTX_OP_WAKE = 3;
constexpr int UTIME_OMIT = -2;
constexpr int WNOHANG = 1;
constexpr int P_PID = 0;
constexpr int PD_DAEMON = 1;
constexpr int PD_CLOEXEC = 2;
constexpr int AT_PHDR = 3;
constexpr int AT_PHENT = 4;
constexpr int AT_PHNUM = 5;
constexpr int AT_PAGESZ = 6;
constexpr int AT_USRSTACKBASE = 35;
constexpr int AT_USRSTACKLIM = 36;
constexpr int S_IFMT = 61440;
constexpr int S_IFDIR = 16384;
constexpr int S_IFREG = 32768;
constexpr int S_IFLNK = 40960;

constexpr long SYS_read = 3;
constexpr long SYS_write = 4;
constexpr long SYS_close = 6;
constexpr long SYS_openat = 499;
constexpr long SYS_fstat = 551;
constexpr long SYS_fstatat = 552;
constexpr long SYS_mkdirat = 496;
constexpr long SYS_unlinkat = 503;
constexpr long SYS_renameat = 501;
constexpr long SYS_linkat = 495;
constexpr long SYS_symlinkat = 502;
constexpr long SYS_readlinkat = 500;
constexpr long SYS_utimensat = 547;
constexpr long SYS_futimens = 546;
constexpr long SYS_fchmodat = 490;
constexpr long SYS_fcntl = 92;
constexpr long SYS_ftruncate = 480;
constexpr long SYS_lseek = 478;
constexpr long SYS_pread = 475;
constexpr long SYS_pwrite = 476;
constexpr long SYS_fstatfs = 556;
constexpr long SYS_getdirentries = 554;
constexpr long SYS_mmap = 477;
constexpr long SYS_munmap = 73;
constexpr long SYS_mprotect = 74;
constexpr long SYS_madvise = 75;
constexpr long SYS_msync = 65;
constexpr long SYS_getpid = 20;
constexpr long SYS_kill = 37;
constexpr long SYS_wait4 = 7;
constexpr long SYS_wait6 = 532;
constexpr long SYS_execve = 59;
constexpr long SYS_fexecve = 492;
constexpr long SYS_fchdir = 13;
constexpr long SYS_dup2 = 90;
constexpr long SYS_pipe2 = 542;
constexpr long SYS_sigaction = 416;
constexpr long SYS_sigprocmask = 340;
constexpr long SYS_nanosleep = 240;
constexpr long SYS_clock_gettime = 232;
constexpr long SYS_clock_getres = 234;
constexpr long SYS_getrandom = 563;
constexpr long SYS_getrusage = 117;
constexpr long SYS_setpriority = 96;
constexpr long SYS_sched_yield = 331;
constexpr long SYS_ppoll = 545;
constexpr long SYS_ioctl = 54;
constexpr long SYS__umtx_op = 454;
constexpr long SYS_flock = 131;
constexpr long SYS___getcwd = 326;
constexpr long SYS_setpgid = 82;
constexpr long SYS___sysctl = 202;
constexpr long SYS___sysctlbyname = 570;
constexpr long SYS_exit = 1;
constexpr long SYS_fork = 2;
constexpr long SYS_pdfork = 518;
constexpr long SYS_thr_self = 432;
constexpr long SYS_thr_new = 455;
constexpr long SYS_thr_exit = 431;
constexpr long SYS_sysarch = 165;

constexpr bool S_ISDIR(u32 m) { return (m & S_IFMT) == S_IFDIR; }
constexpr bool S_ISREG(u32 m) { return (m & S_IFMT) == S_IFREG; }
constexpr bool S_ISLNK(u32 m) { return (m & S_IFMT) == S_IFLNK; }
constexpr bool WIFEXITED(int s)   { return (s & 0177) == 0; }
constexpr int  WEXITSTATUS(int s) { return (s >> 8) & 0xff; }
constexpr bool WIFSIGNALED(int s) { return (s & 0177) != 0177 && (s & 0177) != 0; }
constexpr int  WTERMSIG(int s)    { return s & 0177; }
constexpr int  AMD64_SET_FSBASE = 129;
constexpr int  WEXITED = 16;
// A disposition is a function's address, or one of two values that are not.
inline void (*const SIG_DFL)(int) = nullptr;
inline void (*const SIG_IGN)(int) = reinterpret_cast<void (*)(int)>(1);
constexpr int  WTRAPPED = 32;
constexpr int  NT_FREEBSD_ABI_TAG = 1;
}  // namespace abi

using namespace abi;

struct ktimespec { i64 tv_sec; long tv_nsec; };
struct ktimeval  { i64 tv_sec; long tv_usec; };
struct ksigset   { u32 __bits[4]; };
struct kstat {
    u64 st_dev, st_ino, st_nlink;
    u16 st_mode; i16 st_bsdflags;
    u32 st_uid, st_gid; i32 st_padding1;
    u64 st_rdev;
    ktimespec st_atim, st_mtim, st_ctim, st_birthtim;
    i64 st_size, st_blocks;
    i32 st_blksize; u32 st_flags;
    u64 st_gen; u64 st_spare[10];
};
struct kstatfs {
    u32 f_version, f_type; u64 f_flags, f_bsize, f_iosize, f_blocks, f_bfree; i64 f_bavail;
    unsigned char rest[2344 - 56];
};
struct kdirent { u64 d_fileno; i64 d_off; u16 d_reclen; u8 d_type, d_pad0; u16 d_namlen, d_pad1; char d_name[256]; };
struct krusage { ktimeval ru_utime, ru_stime; long ru_rest[14]; };
struct kwrusage { krusage wru_self, wru_children; };
struct ksigaction { void (*sa_handler)(int); int sa_flags; ksigset sa_mask; };
struct kflock { i64 l_start, l_len; i32 l_pid; i16 l_type, l_whence; i32 l_sysid; };
struct ktermios { u32 c_iflag, c_oflag, c_cflag, c_lflag; u8 c_cc[20]; i32 c_ispeed, c_ospeed; };
struct kwinsize { u16 ws_row, ws_col, ws_xpixel, ws_ypixel; };
struct kpollfd { int fd; short events, revents; };
struct kthr_param {
    void (*start_func)(void*); void* arg; char* stack_base; unsigned long stack_size;
    char* tls_base; unsigned long tls_size; long* child_tid; long* parent_tid; int flags;
    void* rtp; void* spare[3];
};
static_assert(sizeof(kstat) == 224 && __builtin_offsetof(kstat, st_size) == 112 && __builtin_offsetof(kstat, st_mtim) == 64);
static_assert(sizeof(kstatfs) == 2344 && __builtin_offsetof(kstatfs, f_bavail) == 48);
static_assert(sizeof(kdirent) == 280 && __builtin_offsetof(kdirent, d_name) == 24);
static_assert(sizeof(krusage) == 144 && sizeof(kwrusage) == 288);
static_assert(sizeof(ksigaction) == 32 && __builtin_offsetof(ksigaction, sa_mask) == 12);
static_assert(sizeof(kflock) == 32 && __builtin_offsetof(kflock, l_type) == 20);
static_assert(sizeof(ktermios) == 44 && __builtin_offsetof(ktermios, c_cc) == 16);
static_assert(sizeof(kthr_param) == 104 && __builtin_offsetof(kthr_param, flags) == 64);

struct answer { word value; word second; };

#if defined(__x86_64__)
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
#elif defined(__aarch64__)
inline answer call6(word n, word a = 0, word b = 0, word c = 0, word d = 0, word e = 0, word f = 0) {
    register word x8 __asm__("x8") = n;
    register word x0 __asm__("x0") = a;
    register word x1 __asm__("x1") = b;
    register word x2 __asm__("x2") = c;
    register word x3 __asm__("x3") = d;
    register word x4 __asm__("x4") = e;
    register word x5 __asm__("x5") = f;
    word failed;
    __asm__ __volatile__("svc #0\n\tcset %7, cs"
                         : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x8), "=r"(failed)
                         :
                         : "memory", "cc");
    return answer{ failed ? -x0 : x0, x1 };
}
#else
#error "openkal-freebsd issues the system calls of amd64 and arm64"
#endif

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
inline word fstat(int fd, kstat* st)                       { return sys(SYS_fstat, fd, w(st)); }
inline word fstatat(int dir, const char* p, kstat* st, int flags) { return sys(SYS_fstatat, dir, w(p), w(st), flags); }
inline word mkdirat(int dir, const char* p, int mode)      { return sys(SYS_mkdirat, dir, w(p), mode); }
inline word unlinkat(int dir, const char* p, int flags)    { return sys(SYS_unlinkat, dir, w(p), flags); }
inline word renameat(int a, const char* x, int b, const char* y) { return sys(SYS_renameat, a, w(x), b, w(y)); }
inline word linkat(int a, const char* x, int b, const char* y, int flags) { return sys(SYS_linkat, a, w(x), b, w(y), flags); }
inline word symlinkat(const char* t, int dir, const char* p) { return sys(SYS_symlinkat, w(t), dir, w(p)); }
inline word readlinkat(int dir, const char* p, char* b, unsigned long n) { return sys(SYS_readlinkat, dir, w(p), w(b), w(n)); }
inline word utimensat(int dir, const char* p, const ktimespec* t, int flags) { return sys(SYS_utimensat, dir, w(p), w(t), flags); }
inline word futimens(int fd, const ktimespec* t)           { return sys(SYS_futimens, fd, w(t)); }
inline word fchmodat(int dir, const char* p, int mode, int flags) { return sys(SYS_fchmodat, dir, w(p), mode, flags); }
inline word fcntl(int fd, int cmd, word arg = 0)           { return sys(SYS_fcntl, fd, cmd, arg); }
inline word ftruncate(int fd, long long n)                 { return sys(SYS_ftruncate, fd, w(n)); }
inline word lseek(int fd, long long off, int whence)       { return sys(SYS_lseek, fd, w(off), whence); }
inline word pread(int fd, void* b, unsigned long n, long long off) { return sys(SYS_pread, fd, w(b), w(n), w(off)); }
inline word pwrite(int fd, const void* b, unsigned long n, long long off) { return sys(SYS_pwrite, fd, w(b), w(n), w(off)); }
inline word fstatfs(int fd, kstatfs* s)                    { return sys(SYS_fstatfs, fd, w(s)); }
inline word getdirentries(int fd, char* b, unsigned long n, long long* base) { return sys(SYS_getdirentries, fd, w(b), w(n), w(base)); }
inline word mmap(void* a, unsigned long n, int prot, int flags, int fd, long long off) { return sys(SYS_mmap, w(a), w(n), prot, flags, fd, w(off)); }
inline word munmap(void* a, unsigned long n)               { return sys(SYS_munmap, w(a), w(n)); }
inline word mprotect(void* a, unsigned long n, int prot)   { return sys(SYS_mprotect, w(a), w(n), prot); }
inline word madvise(void* a, unsigned long n, int how)     { return sys(SYS_madvise, w(a), w(n), how); }
inline word msync(void* a, unsigned long n, int flags)     { return sys(SYS_msync, w(a), w(n), flags); }
inline word getpid()                                       { return sys(SYS_getpid); }
inline word kill(int pid, int sig)                         { return sys(SYS_kill, pid, sig); }
inline word wait6(int idtype, long id, int* status, int options, kwrusage* ru) {
    return sys(SYS_wait6, idtype, id, w(status), options, w(ru), 0);
}
inline word fexecve(int fd, char* const* av, char* const* ev) { return sys(SYS_fexecve, fd, w(av), w(ev)); }
inline word fchdir(int fd)                                 { return sys(SYS_fchdir, fd); }
inline word dup2(int from, int to)                         { return sys(SYS_dup2, from, to); }
inline word pipe2(int fds[2], int flags)                   { return sys(SYS_pipe2, w(fds), flags); }
inline word sigaction(int sig, const ksigaction* a, ksigaction* old) { return sys(SYS_sigaction, sig, w(a), w(old)); }
inline word sigprocmask(int how, const ksigset* s, ksigset* old) { return sys(SYS_sigprocmask, how, w(s), w(old)); }
inline word nanosleep(const ktimespec* t, ktimespec* rest) { return sys(SYS_nanosleep, w(t), w(rest)); }
inline word clock_gettime(int clock, ktimespec* t)         { return sys(SYS_clock_gettime, clock, w(t)); }
inline word clock_getres(int clock, ktimespec* t)          { return sys(SYS_clock_getres, clock, w(t)); }
inline word getrandom(void* b, unsigned long n, unsigned flags) { return sys(SYS_getrandom, w(b), w(n), flags); }
inline word getrusage(int who, krusage* ru)                { return sys(SYS_getrusage, who, w(ru)); }
inline word setpriority(int which, int who, int prio)      { return sys(SYS_setpriority, which, who, prio); }
inline word sched_yield()                                  { return sys(SYS_sched_yield); }
inline word ppoll(kpollfd* p, unsigned n, const ktimespec* t, const ksigset* m) { return sys(SYS_ppoll, w(p), w(n), w(t), w(m)); }
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
inline word thr_self(long* id)                             { return sys(SYS_thr_self, w(id)); }
inline word thr_new(kthr_param* p)                         { return sys(SYS_thr_new, w(p), sizeof(kthr_param)); }
[[noreturn]] inline void thr_exit(long* state) { for (;;) sys(SYS_thr_exit, w(state)); }
[[noreturn]] inline void exit(int code) { for (;;) sys(SYS_exit, code); }

// pdfork's two answers: the child is told by the second (1 there), and its
// first is then not zero. The descriptor of the started program is written to
// `*pd'; with PD_DAEMON, closing it does not end the program.
inline word pdfork(int* pd, int flags) {
    const answer a = call6(SYS_pdfork, w(pd), flags);
    if (failed(a.value)) return a.value;
    return a.second ? 0 : a.value;
}

}  // namespace okf

using namespace okf::abi;
