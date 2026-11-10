// openkal.fs: names relative to directory descriptors (the *at system calls),
// two preopens --- the working directory and the root --- and listings read
// with getdirentries.
#include "common.h"

namespace {

using okf::word;

struct preopen { char name[okf::kPath]; unsigned long len; int fd; };
preopen g_pre[2];
kal_uintptr g_pre_n = 0;
int g_pre_state = 0;   // 0: not yet, 1: being opened, 2: open
const int kDirFlags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;

void open_preopens() {
    if (__atomic_load_n(&g_pre_state, __ATOMIC_ACQUIRE) == 2) return;
    int expected = 0;
    if (!__atomic_compare_exchange_n(&g_pre_state, &expected, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&g_pre_state, __ATOMIC_ACQUIRE) != 2) okf::sched_yield();
        return;
    }
    // The working directory, by its name, and the root, as the other
    // implementations grant them.
    if (okf::failed(okf::getcwd(g_pre[0].name, sizeof g_pre[0].name))) { g_pre[0].name[0] = '.'; g_pre[0].name[1] = 0; }
    g_pre[0].len = okf::length(g_pre[0].name);
    const word here = okf::openat(AT_FDCWD, ".", kDirFlags);
    g_pre[0].fd = okf::failed(here) ? -1 : static_cast<int>(here);
    g_pre[1].name[0] = '/'; g_pre[1].name[1] = 0; g_pre[1].len = 1;
    const word root = okf::openat(AT_FDCWD, "/", kDirFlags);
    g_pre[1].fd = okf::failed(root) ? -1 : static_cast<int>(root);
    g_pre_n = 2;
    __atomic_store_n(&g_pre_state, 2, __ATOMIC_RELEASE);
}

int kind_of(unsigned mode) {
    if (S_ISREG(mode)) return kal_node_file;
    if (S_ISDIR(mode)) return kal_node_directory;
    if (S_ISLNK(mode)) return kal_node_link;
    return kal_node_other;
}

// The fields an enquiry fills: no more of the structure than the caller stated
// (`self_size'), and, where `mask' is not all of them, only those asked for.
void fill_info(const struct stat& st, kal_node_info* out, kal_u32 mask = ~kal_u32{0}) {
    const kal_u32 cap = out->self_size;
    kal_node_info i{};
    i.self_size = cap;
    i.present = KAL_INFO_KIND | KAL_INFO_SIZE | KAL_INFO_MODIFIED | KAL_INFO_WRITABLE | KAL_INFO_IDENTITY;
    i.kind = kind_of(st.st_mode);
    i.size = static_cast<kal_u64>(st.st_size);
    i.modified_ns = static_cast<kal_u64>(st.st_mtim.tv_sec) * 1000000000ull + static_cast<kal_u64>(st.st_mtim.tv_nsec);
    i.identity[0] = static_cast<kal_u64>(st.st_dev);
    i.identity[1] = static_cast<kal_u64>(st.st_ino);
    i.writable = (st.st_mode & 0222) != 0;
    if (i.kind == kal_node_file) { i.present |= KAL_INFO_EXECUTABLE; i.executable = (st.st_mode & 0111) != 0; }
    i.present &= mask | KAL_INFO_KIND;
    struct { kal_u32 bit; unsigned long end; } const fields[] = {
        { KAL_INFO_SIZE,       offsetof(kal_node_info, size) + 8 },
        { KAL_INFO_MODIFIED,   offsetof(kal_node_info, modified_ns) + 8 },
        { KAL_INFO_IDENTITY,   offsetof(kal_node_info, identity) + 16 },
        { KAL_INFO_KIND,       offsetof(kal_node_info, kind) + sizeof(int) },
        { KAL_INFO_WRITABLE,   offsetof(kal_node_info, writable) + sizeof(int) },
        { KAL_INFO_EXECUTABLE, offsetof(kal_node_info, executable) + sizeof(int) },
    };
    for (const auto& f : fields) if (cap < f.end) i.present &= ~f.bit;
    if (!(i.present & KAL_INFO_SIZE)) i.size = 0;
    if (!(i.present & KAL_INFO_MODIFIED)) i.modified_ns = 0;
    if (!(i.present & KAL_INFO_IDENTITY)) i.identity[0] = i.identity[1] = 0;
    if (!(i.present & KAL_INFO_WRITABLE)) i.writable = 0;
    if (!(i.present & KAL_INFO_EXECUTABLE)) i.executable = 0;
    okf::copy(out, &i, cap < sizeof i ? cap : sizeof i);
}

// A LISTING IN PROGRESS: its own descriptor of the directory, and the entries
// getdirentries last read.
struct lister {
    int fd;
    long pos, end;
    off_t base;
    alignas(8) char buf[8192];
};

int set_time(int dir, const char* name, int fd, kal_u64 ns) {
    struct timespec t[2];
    t[0].tv_sec = 0; t[0].tv_nsec = UTIME_OMIT;
    t[1].tv_sec = static_cast<time_t>(ns / 1000000000ull); t[1].tv_nsec = static_cast<long>(ns % 1000000000ull);
    const word r = name ? okf::utimensat(dir, name, t, 0) : okf::futimens(fd, t);
    return okf::failed(r) ? okf::fail(r) : kal_ok;
}

// The next entry other than `.' and `..', or none (*iter becomes zero).
int next_entry(int dfd, kal_uintptr* iter, char* name, kal_uintptr cap, kal_uintptr* nlen, int* kind, struct stat* st) {
    if (!iter) return kal_err_invalid;
    auto* l = reinterpret_cast<lister*>(*iter);
    if (!l) { if (nlen) *nlen = 0; return kal_ok; }
    for (;;) {
        if (l->pos >= l->end) {
            const word r = okf::getdirentries(l->fd, l->buf, sizeof l->buf, &l->base);
            if (r == -EINTR) continue;
            if (okf::failed(r) || r == 0) {
                okf::close(l->fd);
                kal_free(l, sizeof *l, alignof(lister));
                *iter = 0;
                if (nlen) *nlen = 0;
                return okf::failed(r) ? okf::fail(r) : kal_ok;
            }
            l->pos = 0; l->end = r;
        }
        auto* e = reinterpret_cast<struct dirent*>(l->buf + l->pos);
        if (e->d_reclen == 0) { l->pos = l->end; continue; }
        l->pos += e->d_reclen;
        if (e->d_fileno == 0) continue;
        const unsigned long n = e->d_namlen;
        if ((n == 1 && e->d_name[0] == '.') || (n == 2 && e->d_name[0] == '.' && e->d_name[1] == '.')) continue;
        okf::put(e->d_name, n, name, cap);
        if (nlen) *nlen = n;
        struct stat s{};
        const bool have = !okf::failed(okf::fstatat(dfd, e->d_name, &s, AT_SYMLINK_NOFOLLOW));
        if (kind) *kind = have ? kind_of(s.st_mode) : kal_node_other;
        if (st) { if (have) *st = s; else okf::zero(st, sizeof *st); }
        return kal_ok;
    }
}

}  // namespace

extern "C" {

kal_uintptr kal_fs_preopen_count(void) { open_preopens(); return g_pre_n; }
int kal_fs_preopen(kal_uintptr i, kal_dir* out, char* name, kal_uintptr cap, kal_uintptr* len) {
    open_preopens();
    if (i >= g_pre_n || !out) return kal_err_invalid;
    if (g_pre[i].fd < 0) return kal_err_permission;
    *out = kal_dir{ okf::pack(g_pre[i].fd) };
    okf::put(g_pre[i].name, g_pre[i].len, name, cap);
    if (len) *len = g_pre[i].len;
    return kal_ok;
}
kal_uintptr kal_fs_props(kal_dir) {
    return KAL_FS_PROP_CASE_SENSITIVE | KAL_FS_PROP_LINKS | KAL_FS_PROP_MODIFIED_TIME
         | KAL_FS_PROP_ATOMIC_RENAME | KAL_FS_PROP_MAKE_LINKS | KAL_FS_PROP_LOCKS
         | KAL_FS_PROP_CAPACITY | KAL_FS_PROP_EXECUTABLE | KAL_FS_PROP_REMOVE_OPEN
         | KAL_FS_PROP_HARD_LINKS;
}
kal_uintptr kal_fs_max_name(void) { return okf::kPath - 1; }

int kal_fs_open_dir(kal_dir base, const char* name, kal_uintptr len, kal_dir* out) {
    const int b = okf::unpack(base.h); okf::cname n(name, len);
    if (b < 0 || !out || !n.ok) return kal_err_invalid;
    const word fd = okf::openat(b, n.buf, kDirFlags);
    if (okf::failed(fd)) return okf::fail(fd);
    *out = kal_dir{ okf::pack(static_cast<int>(fd)) };
    return kal_ok;
}
int kal_fs_open(kal_dir base, const char* name, kal_uintptr len, kal_uintptr f, kal_file* out) {
    const int b = okf::unpack(base.h); okf::cname n(name, len);
    if (b < 0 || !out || !n.ok) return kal_err_invalid;
    int o = O_CLOEXEC;
    const bool r = f & KAL_OPEN_READ, w = f & (KAL_OPEN_WRITE | KAL_OPEN_APPEND);
    o |= (r && w) ? O_RDWR : w ? O_WRONLY : O_RDONLY;
    if (f & KAL_OPEN_CREATE)    o |= O_CREAT;
    if (f & KAL_OPEN_EXCLUSIVE) o |= O_EXCL;
    if (f & KAL_OPEN_TRUNCATE)  o |= O_TRUNC;
    if (f & KAL_OPEN_APPEND)    o |= O_APPEND;
    const word fd = okf::openat(b, n.buf, o, 0666);
    if (okf::failed(fd)) return okf::fail(fd);
    struct stat st{};
    if (!okf::failed(okf::fstat(static_cast<int>(fd), &st)) && S_ISDIR(st.st_mode)) {
        okf::close(static_cast<int>(fd));
        return kal_err_is_directory;
    }
    *out = kal_file{ okf::pack(static_cast<int>(fd)) };
    return kal_ok;
}
void kal_fs_close_dir (kal_dir d)  { const int fd = okf::unpack(d.h); if (fd >= 0) okf::close(fd); }
void kal_fs_close_file(kal_file f) { const int fd = okf::unpack(f.h); if (fd >= 0) okf::close(fd); }
kal_stream kal_fs_stream(kal_file f) { return kal_stream{ f.h }; }
int kal_fs_seek(kal_file f, kal_i64 off, int whence, kal_u64* result) {
    const int w = whence == KAL_SEEK_SET ? SEEK_SET : whence == KAL_SEEK_CURRENT ? SEEK_CUR : SEEK_END;
    const word r = okf::lseek(okf::unpack(f.h), off, w);
    if (okf::failed(r)) return okf::fail(r);
    if (result) *result = static_cast<kal_u64>(r);
    return kal_ok;
}
int kal_fs_truncate(kal_file f, kal_u64 size) {
    const word r = okf::ftruncate(okf::unpack(f.h), static_cast<long long>(size));
    return okf::failed(r) ? okf::fail(r) : kal_ok;
}
kal_intptr kal_fs_read_at(kal_file f, void* buf, kal_uintptr len, kal_u64 off) {
    for (;;) {
        const word r = okf::pread(okf::unpack(f.h), buf, len, static_cast<long long>(off));
        if (r == -EINTR) continue;
        return okf::failed(r) ? okf::nfail(r) : r;
    }
}
kal_intptr kal_fs_write_at(kal_file f, const void* buf, kal_uintptr len, kal_u64 off) {
    kal_uintptr done = 0; const char* p = static_cast<const char*>(buf);
    while (done < len) {
        const word r = okf::pwrite(okf::unpack(f.h), p + done, len - done, static_cast<long long>(off + done));
        if (r == -EINTR) continue;
        if (okf::failed(r)) return done ? static_cast<kal_intptr>(done) : okf::nfail(r);
        done += static_cast<kal_uintptr>(r);
    }
    return static_cast<kal_intptr>(done);
}
int kal_fs_info(kal_dir base, const char* name, kal_uintptr len, kal_uintptr flags, kal_u32, kal_node_info* out) {
    const int b = okf::unpack(base.h); okf::cname n(name, len);
    if (b < 0 || !out || !n.ok) return kal_err_invalid;
    struct stat st{};
    const word r = okf::fstatat(b, n.buf, &st, (flags & KAL_FS_NO_RESOLVE) ? AT_SYMLINK_NOFOLLOW : 0);
    if (okf::failed(r)) {
        if (r == -ENOENT) {
            const kal_u32 cap = out->self_size;
            kal_node_info i{}; i.self_size = cap;
            i.present = KAL_INFO_KIND; i.kind = kal_node_absent;
            okf::copy(out, &i, cap < sizeof i ? cap : sizeof i);
            return kal_ok;
        }
        return okf::fail(r);
    }
    fill_info(st, out);
    return kal_ok;
}
int kal_fs_file_info(kal_file f, kal_u32, kal_node_info* out) {
    if (!out) return kal_err_invalid;
    struct stat st{};
    const word r = okf::fstat(okf::unpack(f.h), &st);
    if (okf::failed(r)) return okf::fail(r);
    fill_info(st, out);
    return kal_ok;
}
int kal_fs_mkdir(kal_dir base, const char* name, kal_uintptr len) {
    okf::cname n(name, len); if (!n.ok) return kal_err_invalid;
    const word r = okf::mkdirat(okf::unpack(base.h), n.buf, 0777);
    return okf::failed(r) ? okf::fail(r) : kal_ok;
}
int kal_fs_remove(kal_dir base, const char* name, kal_uintptr len) {
    okf::cname n(name, len); if (!n.ok) return kal_err_invalid;
    const int b = okf::unpack(base.h);
    word r = okf::unlinkat(b, n.buf, 0);
    if (!okf::failed(r)) return kal_ok;
    if (r == -EISDIR || r == -EPERM) {
        const word d = okf::unlinkat(b, n.buf, AT_REMOVEDIR);
        if (!okf::failed(d)) return kal_ok;
        if (d != -ENOTDIR) r = d;
    }
    return okf::fail(r);
}
int kal_fs_rename(kal_dir from, const char* a, kal_uintptr al, kal_dir to, const char* b, kal_uintptr bl) {
    okf::cname x(a, al), y(b, bl); if (!x.ok || !y.ok) return kal_err_invalid;
    const word r = okf::renameat(okf::unpack(from.h), x.buf, okf::unpack(to.h), y.buf);
    return okf::failed(r) ? okf::fail(r) : kal_ok;
}
// FreeBSD has no rename that refuses to replace, so the property is not
// claimed and the flag is refused as unsupported.
int kal_fs_rename_flags(kal_dir from, const char* a, kal_uintptr al, kal_dir to, const char* b, kal_uintptr bl, kal_uintptr flags) {
    if (flags == 0) return kal_fs_rename(from, a, al, to, b, bl);
    return flags == KAL_RENAME_NO_REPLACE ? kal_err_not_supported : kal_err_invalid;
}
int kal_fs_set_modified(kal_file f, kal_u64 ns) { return set_time(-1, nullptr, okf::unpack(f.h), ns); }
int kal_fs_set_modified_at(kal_dir base, const char* name, kal_uintptr len, kal_u64 ns) {
    okf::cname n(name, len); if (!n.ok) return kal_err_invalid;
    return set_time(okf::unpack(base.h), n.buf, -1, ns);
}
int kal_fs_set_executable_at(kal_dir base, const char* name, kal_uintptr len, int executable) {
    okf::cname n(name, len); if (!n.ok) return kal_err_invalid;
    struct stat st{}; const int b = okf::unpack(base.h);
    const word r = okf::fstatat(b, n.buf, &st, 0);
    if (okf::failed(r)) return okf::fail(r);
    if (S_ISDIR(st.st_mode)) return kal_err_is_directory;
    unsigned m = st.st_mode & 07777;
    if (executable) m |= (m & 0444) >> 2; else m &= ~0111u;
    const word c = okf::fchmodat(b, n.buf, static_cast<int>(m), 0);
    return okf::failed(c) ? okf::fail(c) : kal_ok;
}
// A LOCK BELONGS TO THE OPEN FILE, as openkal states it. FreeBSD's flock does
// (whole files), and its record locks belong to the process; a lock upon part
// of a file is the record lock, upon the whole of it flock.
int kal_fs_lock(kal_file f, kal_u64 start, kal_u64 len, kal_uintptr mode) {
    const int fd = okf::unpack(f.h);
    if (fd < 0) return kal_err_invalid;
    const kal_uintptr kinds = mode & (KAL_LOCK_SHARED | KAL_LOCK_EXCLUSIVE);
    if (kinds != KAL_LOCK_SHARED && kinds != KAL_LOCK_EXCLUSIVE) return kal_err_invalid;
    if (mode & ~(KAL_LOCK_SHARED | KAL_LOCK_EXCLUSIVE | KAL_LOCK_WAIT)) return kal_err_invalid;
    const bool exclusive = kinds == KAL_LOCK_EXCLUSIVE, wait = mode & KAL_LOCK_WAIT;
    if (start == 0 && len == 0) {
        for (;;) {
            const word r = okf::flock(fd, (exclusive ? LOCK_EX : LOCK_SH) | (wait ? 0 : LOCK_NB));
            if (r == -EINTR) continue;
            if (okf::failed(r)) return r == -EWOULDBLOCK ? kal_err_again : okf::fail(r);
            return kal_ok;
        }
    }
    struct flock l{}; l.l_type = exclusive ? F_WRLCK : F_RDLCK;
    l.l_whence = SEEK_SET; l.l_start = static_cast<off_t>(start); l.l_len = static_cast<off_t>(len);
    for (;;) {
        const word r = okf::fcntl(fd, wait ? F_SETLKW : F_SETLK, okf::w(&l));
        if (r == -EINTR) continue;
        if (okf::failed(r)) return (r == -EACCES || r == -EAGAIN) ? kal_err_again : okf::fail(r);
        return kal_ok;
    }
}
int kal_fs_unlock(kal_file f, kal_u64 start, kal_u64 len) {
    const int fd = okf::unpack(f.h);
    if (fd < 0) return kal_err_invalid;
    if (start == 0 && len == 0) {
        const word r = okf::flock(fd, LOCK_UN);
        return okf::failed(r) ? okf::fail(r) : kal_ok;
    }
    struct flock l{}; l.l_type = F_UNLCK; l.l_whence = SEEK_SET;
    l.l_start = static_cast<off_t>(start); l.l_len = static_cast<off_t>(len);
    const word r = okf::fcntl(fd, F_SETLK, okf::w(&l));
    return okf::failed(r) ? okf::fail(r) : kal_ok;
}
int kal_fs_capacity(kal_dir d, kal_u64* total, kal_u64* avail) {
    struct statfs v{};
    const word r = okf::fstatfs(okf::unpack(d.h), &v);
    if (okf::failed(r)) return okf::fail(r);
    if (total) *total = static_cast<kal_u64>(v.f_blocks) * static_cast<kal_u64>(v.f_bsize);
    if (avail) *avail = (v.f_bavail > 0 ? static_cast<kal_u64>(v.f_bavail) : 0) * static_cast<kal_u64>(v.f_bsize);
    return kal_ok;
}
int kal_fs_link_create(kal_dir base, const char* name, kal_uintptr len, const char* target, kal_uintptr tlen, kal_uintptr) {
    okf::cname n(name, len);
    if (!n.ok || !target || tlen == 0 || tlen >= okf::kPath) return kal_err_invalid;
    char t[okf::kPath]; okf::copy(t, target, tlen); t[tlen] = 0;
    const word r = okf::symlinkat(t, okf::unpack(base.h), n.buf);
    return okf::failed(r) ? okf::fail(r) : kal_ok;
}
kal_intptr kal_fs_link_read(kal_dir base, const char* name, kal_uintptr len, char* out, kal_uintptr cap) {
    okf::cname n(name, len); if (!n.ok) return -kal_err_invalid;
    char t[okf::kPath];
    const word r = okf::readlinkat(okf::unpack(base.h), n.buf, t, sizeof t);
    if (okf::failed(r)) return okf::nfail(r);
    return okf::put(t, static_cast<kal_uintptr>(r), out, cap);
}
int kal_fs_hard_link(kal_dir from, const char* a, kal_uintptr al, kal_dir to, const char* b, kal_uintptr bl) {
    okf::cname x(a, al), y(b, bl);
    const int f = okf::unpack(from.h), t = okf::unpack(to.h);
    if (f < 0 || t < 0 || !x.ok || !y.ok) return kal_err_invalid;
    // A directory is refused before the call: the kernel's answer (EPERM)
    // names a permission. Two volumes are the kernel's EXDEV.
    struct stat st{};
    if (!okf::failed(okf::fstatat(f, x.buf, &st, AT_SYMLINK_NOFOLLOW)) && S_ISDIR(st.st_mode)) return kal_err_is_directory;
    const word r = okf::linkat(f, x.buf, t, y.buf, 0);
    if (!okf::failed(r)) return kal_ok;
    return r == -EXDEV ? kal_err_not_supported : okf::fail(r);
}
kal_intptr kal_fs_resolved_name(kal_dir, const char*, kal_uintptr, char*, kal_uintptr) { return -kal_err_not_supported; }
int kal_fs_dir_attenuate(kal_dir, const kal_dir_rights*, kal_dir*) { return kal_err_not_supported; }

int kal_fs_list_begin(kal_dir d, kal_uintptr* iter) {
    const int fd = okf::unpack(d.h);
    if (fd < 0 || !iter) return kal_err_invalid;
    const word own = okf::openat(fd, ".", kDirFlags);
    if (okf::failed(own)) return okf::fail(own);
    auto* l = static_cast<lister*>(kal_alloc(sizeof(lister), alignof(lister)));
    if (!l) { okf::close(static_cast<int>(own)); return kal_err_no_memory; }
    l->fd = static_cast<int>(own); l->pos = 0; l->end = 0; l->base = 0;
    *iter = reinterpret_cast<kal_uintptr>(l);
    return kal_ok;
}
int kal_fs_list_next(kal_dir d, kal_uintptr* iter, char* name, kal_uintptr cap, kal_uintptr* nlen, int* kind) {
    if (okf::unpack(d.h) < 0) return kal_err_invalid;
    return next_entry(okf::unpack(d.h), iter, name, cap, nlen, kind, nullptr);
}
int kal_fs_list_next_info(kal_dir d, kal_uintptr* iter, char* name, kal_uintptr cap, kal_uintptr* nlen, kal_u32 wanted, kal_node_info* info) {
    if (okf::unpack(d.h) < 0 || !info) return kal_err_invalid;
    struct stat st{}; int kind = 0;
    const int r = next_entry(okf::unpack(d.h), iter, name, cap, nlen, &kind, &st);
    if (r == kal_ok && *iter) fill_info(st, info, wanted);
    return r;
}

}  // extern "C"
