// THE THREAD-LOCAL STORAGE OF A PROGRAM THAT HAS NO RUNTIME, in the standalone
// arrangement: the image's PT_TLS segment, laid out as FreeBSD lays it out
// (variant II on amd64: the thread pointer at the end of the block, pointing
// to itself; variant I on arm64: a 16-byte control block, then the block), and
// the thread pointer set (sysarch AMD64_SET_FSBASE; tpidr_el0).
#pragma once
#include "sys.h"

namespace okf {

unsigned long auxval(unsigned long key);

struct tls_image { const unsigned char* data; unsigned long filesz, memsz, align; bool known; };
inline tls_image& image() { static tls_image i{}; return i; }

inline unsigned long round_up(unsigned long n, unsigned long to) { return (n + to - 1) & ~(to - 1); }

inline void describe_tls() {
    auto& im = image();
    if (im.known) return;
    im.known = true;
    struct elf_phdr { u32 type, flags; u64 offset, vaddr, paddr, filesz, memsz, align; };
    const unsigned long phdr = auxval(AT_PHDR), phent = auxval(AT_PHENT), phnum = auxval(AT_PHNUM);
    if (phdr == 0 || phnum == 0) return;
    for (unsigned long i = 0; i < phnum; ++i) {
        const auto* p = reinterpret_cast<const elf_phdr*>(phdr + i * phent);
        if (p->type != 7 /* PT_TLS */) continue;
        im.data = reinterpret_cast<const unsigned char*>(p->vaddr);
        im.filesz = p->filesz; im.memsz = p->memsz; im.align = p->align;
        return;
    }
}

struct tls_block { void* base; unsigned long bytes; unsigned long align; void* tp; };

inline tls_block make_tls(void* (*alloc)(unsigned long, unsigned long)) {
    describe_tls();
    const auto& im = image();
    const unsigned long laid_out = im.align ? im.align : 1;
    const unsigned long align = laid_out < 16 ? 16 : laid_out;
    tls_block b{};
    b.align = align;
#if defined(__x86_64__)
    // The thread pointer is the address of a word holding itself, whether or
    // not the image has a segment: every amd64 program reads %fs:0.
    const unsigned long size = round_up(im.memsz, laid_out);
    b.bytes = size + 64;
    b.base = alloc(b.bytes, align);
    if (!b.base) return b;
    auto* base = static_cast<unsigned char*>(b.base);
    for (unsigned long i = 0; i < b.bytes; ++i) base[i] = 0;
    if (im.data && im.filesz) for (unsigned long i = 0; i < im.filesz; ++i) base[i] = im.data[i];
    b.tp = base + size;
    *reinterpret_cast<void**>(b.tp) = b.tp;
#elif defined(__aarch64__)
    const unsigned long gap = round_up(16, align);
    b.bytes = gap + round_up(im.memsz, laid_out) + 64;
    b.base = alloc(b.bytes, align);
    if (!b.base) return b;
    auto* base = static_cast<unsigned char*>(b.base);
    for (unsigned long i = 0; i < b.bytes; ++i) base[i] = 0;
    if (im.data && im.filesz) for (unsigned long i = 0; i < im.filesz; ++i) base[gap + i] = im.data[i];
    b.tp = base;
#endif
    return b;
}

inline void set_thread_pointer(void* tp) {
#if defined(__x86_64__)
    void* base = tp;
    sys(SYS_sysarch, AMD64_SET_FSBASE, w(&base));
#elif defined(__aarch64__)
    __asm__ __volatile__("msr tpidr_el0, %0" :: "r"(tp));
#endif
}

}  // namespace okf
