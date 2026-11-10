// THE STANDALONE ARRANGEMENT'S BEGINNING (feature `standalone'): the program
// has no runtime, and this implementation receives control from the kernel.
//
// FreeBSD passes the address of the argument block --- argc, the arguments, a
// null, the environment, a null, the auxiliary vector --- in %rdi on amd64 and
// in x0 on arm64. The thread pointer is set before anything with thread-local
// state runs; then a C library's own hand-over is used where one is linked
// (`__libc_start_main', openkal-musl's), and otherwise the image's initialisers
// are run and `main' is called.
#include "common.h"

#ifdef OKF_STANDALONE
#include "tls.h"

// THE BRAND. The kernel recognises a FreeBSD image by this note (the ABI tag,
// "FreeBSD" and the __FreeBSD_version it was built for: 14.0-RELEASE).
__asm__(
    ".section .note.tag,\"a\",%note\n"
    ".p2align 2\n"
    ".long 8\n"          // the name's size, "FreeBSD" and its terminator
    ".long 4\n"          // the description's size
    ".long 1\n"          // NT_FREEBSD_ABI_TAG
    ".asciz \"FreeBSD\"\n"
    ".long 1400097\n"
    ".previous\n");

extern "C" {
int main(int, char**, char**);
[[gnu::weak]] int __libc_start_main(int (*)(int, char**, char**), int, char**,
                                    void (*)(), void (*)(), void (*)());
[[noreturn]] void __okf_start_c(long* block);
}

namespace {

using initialiser = void (*)(int, char**, char**);
extern "C" {
[[gnu::weak]] extern initialiser __preinit_array_start[];
[[gnu::weak]] extern initialiser __preinit_array_end[];
[[gnu::weak]] extern initialiser __init_array_start[];
[[gnu::weak]] extern initialiser __init_array_end[];
}

void* allocate(unsigned long n, unsigned long a) { return kal_alloc(n, a); }

}  // namespace

#if defined(__x86_64__)
__asm__(
".text\n"
".globl _start\n"
".type _start,@function\n"
"_start:\n"
"  xor %ebp,%ebp\n"
"  and $-16,%rsp\n"
"  call __okf_start_c\n"
"  hlt\n"
".size _start,.-_start\n");
#elif defined(__aarch64__)
__asm__(
".text\n"
".globl _start\n"
".type _start,%function\n"
"_start:\n"
"  mov x29,#0\n"
"  mov x30,#0\n"
"  mov x1,sp\n"
"  and x1,x1,#-16\n"
"  mov sp,x1\n"
"  bl __okf_start_c\n"
"  brk #0\n"
".size _start,.-_start\n");
#endif

extern "C" [[noreturn]] void __okf_start_c(long* block) {
    const int argc = static_cast<int>(block[0]);
    char** argv = reinterpret_cast<char**>(block + 1);
    char** envp = argv + argc + 1;
    char** e = envp;
    while (*e) ++e;
    okf::g_argc = argc; okf::g_argv = argv; okf::g_envp = envp;
    okf::g_auxv = reinterpret_cast<unsigned long*>(e + 1);

    const okf::tls_block b = okf::make_tls(allocate);
    if (b.tp != nullptr) okf::set_thread_pointer(b.tp);

    if (__libc_start_main != nullptr) {
        __libc_start_main(main, argc, argv, nullptr, nullptr, nullptr);
        kal_exit(127);   // a C library's hand-over does not return
    }
    for (initialiser* p = __preinit_array_start; p != __preinit_array_end; ++p) (*p)(argc, argv, envp);
    for (initialiser* p = __init_array_start;    p != __init_array_end;    ++p) (*p)(argc, argv, envp);
    kal_exit(main(argc, argv, envp));
}

#endif  // OKF_STANDALONE

namespace okf {
unsigned long* g_auxv = nullptr;
// A value of the auxiliary vector, or zero. Recorded only where this
// implementation received control (standalone).
unsigned long auxval(unsigned long key) {
    for (unsigned long* a = g_auxv; a && a[0] != 0; a += 2) if (a[0] == key) return a[1];
    return 0;
}
}  // namespace okf
