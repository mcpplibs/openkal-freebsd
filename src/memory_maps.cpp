// openkal.exec, openkal.map and openkal.region: mappings and their protection.
//
// Memory a loader writes and then executes is never writable and executable at
// once (W^X): it is mapped writable and published read and execute. A region at
// an address is FreeBSD's MAP_FIXED | MAP_EXCL, which refuses what is already
// mapped there.
#include "common.h"

namespace {
using okf::word;
kal_uintptr page() { return kal_memory_granularity(); }
}  // namespace

extern "C" {

// ================================================================ exec
kal_uintptr kal_exec_granularity(void) { return page(); }
void* kal_exec_alloc(kal_uintptr size) {
    if (size == 0) return nullptr;
    const word p = okf::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    return okf::failed(p) ? nullptr : reinterpret_cast<void*>(p);
}
void* kal_exec_alloc_at(void* at, kal_uintptr size) {
    if (!at || !size || reinterpret_cast<kal_uintptr>(at) % page()) return nullptr;
    const word p = okf::mmap(at, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED | MAP_EXCL, -1, 0);
    if (okf::failed(p)) return nullptr;
    if (reinterpret_cast<void*>(p) != at) { okf::munmap(reinterpret_cast<void*>(p), size); return nullptr; }
    return reinterpret_cast<void*>(p);
}
int kal_exec_publish(void* p, kal_uintptr size) {
    const word r = okf::mprotect(p, size, PROT_READ | PROT_EXEC);
    return okf::failed(r) ? okf::fail(r) : kal_ok;
}
int kal_exec_publish_part(void* r, kal_uintptr off, kal_uintptr size) {
    const kal_uintptr g = page();
    if (!r || off % g || size % g) return kal_err_invalid;
    const word e = okf::mprotect(static_cast<char*>(r) + off, size, PROT_READ | PROT_EXEC);
    return okf::failed(e) ? okf::fail(e) : kal_ok;
}
int kal_exec_seal_part(void* r, kal_uintptr off, kal_uintptr size, kal_uintptr flags) {
    const kal_uintptr g = page();
    if (flags & ~KAL_EXEC_SEAL_FOREVER) return kal_err_invalid;
    if (!r || off % g || size % g) return kal_err_invalid;
    const word e = okf::mprotect(static_cast<char*>(r) + off, size, PROT_READ);
    return okf::failed(e) ? okf::fail(e) : kal_ok;
}
int kal_exec_map_part(void* r, kal_uintptr off, kal_uintptr size, kal_file f, kal_u64 at, kal_uintptr how) {
    const kal_uintptr g = page();
    if (!r || size == 0 || off % g || size % g || at % KAL_MAP_ALIGNMENT) return kal_err_invalid;
    if (how & ~(KAL_EXEC_MAP_EXECUTE | KAL_EXEC_MAP_PREFETCH | KAL_EXEC_MAP_BTI | KAL_EXEC_MAP_WRITE))
        return kal_err_invalid;
    if ((how & KAL_EXEC_MAP_WRITE) && (how & KAL_EXEC_MAP_EXECUTE)) return kal_err_invalid;
    struct stat st{};
    const word s = okf::fstat(okf::unpack(f.h), &st);
    if (okf::failed(s)) return okf::fail(s);
    if (at + size > static_cast<kal_u64>(st.st_size)) return kal_err_invalid;
    int prot = PROT_READ;
    if (how & KAL_EXEC_MAP_EXECUTE) prot |= PROT_EXEC;
    if (how & KAL_EXEC_MAP_WRITE)   prot |= PROT_WRITE;
    // BTI is an ARM feature this amd64 implementation does not enforce, and the
    // part is mapped as without it.
    const word p = okf::mmap(static_cast<char*>(r) + off, size, prot, MAP_PRIVATE | MAP_FIXED, okf::unpack(f.h),
                             static_cast<long long>(at));
    if (okf::failed(p)) return okf::fail(p);
    if (how & KAL_EXEC_MAP_PREFETCH) okf::madvise(reinterpret_cast<void*>(p), size, MADV_WILLNEED);
    return kal_ok;
}
void kal_exec_free(void* p, kal_uintptr size) { if (p) okf::munmap(p, size); }
kal_uintptr kal_exec_props(void) {
    return KAL_EXEC_PROP_AVAILABLE | KAL_EXEC_PROP_FILE | KAL_EXEC_PROP_REPUBLISH | KAL_EXEC_PROP_PLACE;
}

// ================================================================ map
int kal_map_file(kal_file f, kal_u64 offset, kal_uintptr length, kal_uintptr how, void** out) {
    if (!out || length == 0 || offset % KAL_MAP_ALIGNMENT || (how & ~(KAL_MAP_WRITE | KAL_MAP_SHARED))) return kal_err_invalid;
    struct stat st{};
    const word s = okf::fstat(okf::unpack(f.h), &st);
    if (okf::failed(s)) return okf::fail(s);
    if (offset + length > static_cast<kal_u64>(st.st_size)) return kal_err_invalid;
    const int prot = PROT_READ | ((how & KAL_MAP_WRITE) ? PROT_WRITE : 0);
    const word p = okf::mmap(nullptr, length, prot, (how & KAL_MAP_SHARED) ? MAP_SHARED : MAP_PRIVATE,
                             okf::unpack(f.h), static_cast<long long>(offset));
    if (okf::failed(p)) return p == -ENODEV ? kal_err_not_supported : okf::fail(p);
    *out = reinterpret_cast<void*>(p);
    return kal_ok;
}
int kal_map_sync(void* region, kal_uintptr length) {
    const word r = okf::msync(region, length, MS_SYNC);
    return okf::failed(r) ? okf::fail(r) : kal_ok;
}
void kal_map_release(void* region, kal_uintptr length) { if (region) okf::munmap(region, length); }

// ================================================================ region
// Address space reserved without backing (PROT_NONE), backed in parts.
void* kal_region_reserve(kal_uintptr size, kal_uintptr align, kal_uintptr flags) {
    if (flags & ~KAL_REGION_COMMIT) return nullptr;
    const kal_uintptr g = page();
    if (size == 0 || size % g) return nullptr;
    if (align < g) align = g;
    if (align & (align - 1)) return nullptr;
    const int prot = (flags & KAL_REGION_COMMIT) ? (PROT_READ | PROT_WRITE) : PROT_NONE;
    const kal_uintptr span = size + (align > g ? align : 0);
    const word m = okf::mmap(nullptr, span, prot, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (okf::failed(m)) return nullptr;
    char* p = reinterpret_cast<char*>(m);
    if (span == size) return p;
    char* a = reinterpret_cast<char*>((reinterpret_cast<kal_uintptr>(p) + align - 1) & ~(align - 1));
    if (a > p) okf::munmap(p, static_cast<kal_uintptr>(a - p));
    if (a + size < p + span) okf::munmap(a + size, static_cast<kal_uintptr>(p + span - (a + size)));
    return a;
}
// A part begins and ends at multiples of the granularity; FreeBSD's mprotect
// would round an address that does not, so it is refused here.
static bool whole_pages(void* addr, kal_uintptr len) {
    const kal_uintptr g = page();
    return addr && len && reinterpret_cast<kal_uintptr>(addr) % g == 0 && len % g == 0;
}
int kal_region_commit(void* addr, kal_uintptr len) {
    if (!whole_pages(addr, len)) return kal_err_invalid;
    const word r = okf::mprotect(addr, len, PROT_READ | PROT_WRITE);
    return okf::failed(r) ? okf::fail(r) : kal_ok;
}
int kal_region_decommit(void* addr, kal_uintptr len) {
    if (!whole_pages(addr, len)) return kal_err_invalid;
    // Replaced by fresh reserved pages: the contents are gone and the backing
    // is returned.
    const word p = okf::mmap(addr, len, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    return okf::failed(p) ? okf::fail(p) : kal_ok;
}
int kal_region_discard(void* addr, kal_uintptr len) {
    if (!whole_pages(addr, len)) return kal_err_invalid;
    // The contents need not be kept; the part stays backed.
    okf::madvise(addr, len, MADV_FREE);
    return kal_ok;
}
void kal_region_release(void* addr, kal_uintptr size) { if (addr) okf::munmap(addr, size); }
kal_uintptr kal_region_props(void) { return KAL_REGION_PROP_OVERCOMMIT; }

}  // extern "C"
