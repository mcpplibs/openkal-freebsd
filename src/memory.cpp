// openkal.memory: blocks of size classes carved from mappings, and mappings of
// their own for the large; the machine's memory from sysctl.
#include "common.h"

namespace {

constexpr unsigned long kPage     = 4096;
constexpr unsigned long kMinBlock = 16;
constexpr unsigned long kMaxSmall = 32768;
constexpr unsigned long kChunk    = 1u << 20;
constexpr int           kClasses  = 12;   // 16, 32, ... 32768

okf::spin      g_lock;
void*          g_free[kClasses];
unsigned char* g_next[kClasses];
unsigned char* g_end[kClasses];

int class_of(unsigned long n) { unsigned long s = kMinBlock; int c = 0; while (s < n) { s <<= 1; ++c; } return c; }
unsigned long size_of(int c) { return kMinBlock << c; }
unsigned long round_up(unsigned long n, unsigned long to) { return (n + to - 1) & ~(to - 1); }

void* map(unsigned long bytes) {
    const okf::word r = okf::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    return okf::failed(r) ? nullptr : reinterpret_cast<void*>(r);
}
void unmap(void* p, unsigned long bytes) { okf::munmap(p, bytes); }

// A class's blocks are carved in turn from a chunk of their own, so a block of
// a class is aligned to the class's size up to the page (chunks are mapped
// whole pages, and every class divides the chunk).
void* carve(int c) {
    const unsigned long bs = size_of(c);
    if (g_next[c] == g_end[c]) {
        auto* base = static_cast<unsigned char*>(map(kChunk));
        if (!base) return nullptr;
        g_next[c] = base;
        g_end[c] = base + kChunk;
    }
    void* b = g_next[c];
    g_next[c] += bs;
    return b;
}

bool sysctl_u64(const char* name, kal_u64& out) {
    unsigned long v = 0, n = sizeof v;
    if (okf::failed(okf::sysctlbyname(name, &v, &n))) return false;
    if (n == 4) { unsigned u = 0; okf::copy(&u, &v, 4); out = u; }
    else out = v;
    return true;
}

}  // namespace

extern "C" {

void* kal_alloc(kal_uintptr size, kal_uintptr align) {
    if (size == 0) return nullptr;
    if (align < kMinBlock) align = kMinBlock;
    if (align & (align - 1)) return nullptr;
    const unsigned long want = size < align ? align : size;
    if (want <= kMaxSmall && align <= kPage) {
        const int c = class_of(want);
        g_lock.lock();
        void* b = g_free[c];
        if (b) g_free[c] = *reinterpret_cast<void**>(b);
        else b = carve(c);
        g_lock.unlock();
        return b;
    }
    const unsigned long bytes = round_up(size, kPage);
    if (align <= kPage) return map(bytes);
    // An alignment wider than a page: map more, and record before the region
    // what is to be released.
    const unsigned long total = bytes + align;
    auto* base = static_cast<unsigned char*>(map(total));
    if (!base) return nullptr;
    auto addr = reinterpret_cast<unsigned long>(base) + 16;
    addr = (addr + align - 1) & ~(align - 1);
    auto* user = reinterpret_cast<unsigned long*>(addr);
    user[-1] = total;
    user[-2] = reinterpret_cast<unsigned long>(base);
    return user;
}

void kal_free(void* p, kal_uintptr size, kal_uintptr align) {
    if (!p || size == 0) return;
    if (align < kMinBlock) align = kMinBlock;
    const unsigned long want = size < align ? align : size;
    if (want <= kMaxSmall && align <= kPage) {
        const int c = class_of(want);
        g_lock.lock();
        *reinterpret_cast<void**>(p) = g_free[c];
        g_free[c] = p;
        g_lock.unlock();
        return;
    }
    if (align <= kPage) { unmap(p, round_up(size, kPage)); return; }
    auto* user = static_cast<unsigned long*>(p);
    unmap(reinterpret_cast<void*>(user[-2]), user[-1]);
}

kal_uintptr kal_memory_granularity(void) {
    kal_u64 page = 0;
    return sysctl_u64("hw.pagesize", page) && page ? static_cast<kal_uintptr>(page) : kPage;
}

// The machine's memory (hw.physmem), and what is available now: the pages the
// system holds free and those it would give up first (inactive).
int kal_memory_info(kal_memory_status* out) {
    if (out == nullptr || out->self_size < 2 * sizeof(kal_u32)) return kal_err_invalid;
    const kal_u32 self = out->self_size;
    kal_memory_status v{};
    v.self_size = self;
    kal_u64 total = 0, page = 0, free = 0, inactive = 0;
    if (sysctl_u64("hw.physmem", total) && total) { v.total_bytes = total; v.present |= KAL_MSTATUS_TOTAL; }
    if (sysctl_u64("hw.pagesize", page) && sysctl_u64("vm.stats.vm.v_free_count", free)) {
        sysctl_u64("vm.stats.vm.v_inactive_count", inactive);
        v.available_bytes = (free + inactive) * page;
        if ((v.present & KAL_MSTATUS_TOTAL) && v.available_bytes > v.total_bytes) v.available_bytes = v.total_bytes;
        v.present |= KAL_MSTATUS_AVAILABLE;
    }
    if (self < offsetof(kal_memory_status, total_bytes) + 8) v.present &= ~KAL_MSTATUS_TOTAL;
    if (self < offsetof(kal_memory_status, available_bytes) + 8) v.present &= ~KAL_MSTATUS_AVAILABLE;
    okf::copy(out, &v, self < sizeof v ? self : sizeof v);
    return kal_ok;
}

}  // extern "C"
