// openkal.task: contexts, and a word waited upon through _umtx_op.
//
// IN THE ARRANGEMENT THIS VERSION IS BUILT FOR, the program carries a runtime
// that has received control and creates its threads (FreeBSD's libthr), and
// this implementation borrows both, as openkal-linux does above a C library: a
// context is the runtime's thread, and its stack is the one the runtime states.
// Every wait and wake is the kernel's own (_umtx_op on a private word), so a
// word is woken from a signal handler too.
#include "common.h"
#include <pthread.h>
#include <pthread_np.h>

namespace {

using okf::word;

struct start_box { void (*entry)(void*); void* arg; };

void* trampoline(void* p) {
    start_box b = *static_cast<start_box*>(p);
    kal_free(p, sizeof(start_box), alignof(start_box));
    b.entry(b.arg);
    return nullptr;
}

// THE SIZE A CONTEXT IS GIVEN WHEN IT ASKS FOR NONE: the host's own default,
// 8 MiB, which a request below it is raised to (kal_task_start_sized states a
// least size).
constexpr kal_uintptr kDefaultStack = 8u * 1024u * 1024u;

// HOW MANY CONTEXTS WAIT, counted where they wait. The kernel's wake does not
// say how many it woke, and kal_task_wake reports it: each wait is counted in a
// bucket its word's address selects, and a wake reports at most that count.
unsigned g_waiting[64];
unsigned* waiting_on(const kal_u32* w) { return &g_waiting[(reinterpret_cast<kal_uintptr>(w) >> 2) % 64]; }

}  // namespace

extern "C" {

int kal_task_start_sized(void (*entry)(void*), void* arg, kal_uintptr size, kal_task* out) {
    if (!entry || !out) return kal_err_invalid;
    if (size > (kal_uintptr(1) << 46)) return kal_err_no_memory;
    auto* b = static_cast<start_box*>(kal_alloc(sizeof(start_box), alignof(start_box)));
    if (!b) return kal_err_no_memory;
    b->entry = entry; b->arg = arg;
    pthread_attr_t a;
    pthread_attr_init(&a);
    const kal_uintptr page = 4096;
    kal_uintptr want = size < kDefaultStack ? kDefaultStack : (size + page - 1) & ~(page - 1);
    pthread_attr_setstacksize(&a, want);
    pthread_t t;
    const int r = pthread_create(&t, &a, trampoline, b);
    pthread_attr_destroy(&a);
    if (r != 0) {
        kal_free(b, sizeof(start_box), alignof(start_box));
        return (r == ENOMEM || r == EAGAIN) ? kal_err_no_memory : okf::translate(r);
    }
    *out = kal_task{ reinterpret_cast<kal_uintptr>(t) };
    return kal_ok;
}
int kal_task_start(void (*entry)(void*), void* arg, kal_task* out) { return kal_task_start_sized(entry, arg, 0, out); }
int kal_task_join(kal_task t) {
    if (t.h == 0) return kal_err_invalid;
    return pthread_join(reinterpret_cast<pthread_t>(t.h), nullptr) == 0 ? kal_ok : kal_err_invalid;
}
void kal_task_yield(void) { okf::sched_yield(); }
// The context's identifier, the kernel's (thr_self).
kal_uintptr kal_task_current(void) {
    long id = 0;
    okf::sys(SYS_thr_self, okf::w(&id));
    return static_cast<kal_uintptr>(id);
}
int kal_task_stack(void** base, kal_uintptr* size) {
    if (!base || !size) return kal_err_invalid;
    pthread_attr_t a;
    pthread_attr_init(&a);
    void* addr = nullptr; size_t sz = 0;
    const bool ok = pthread_attr_get_np(pthread_self(), &a) == 0 && pthread_attr_getstack(&a, &addr, &sz) == 0;
    pthread_attr_destroy(&a);
    if (!ok) return kal_err_io;
    *base = addr; *size = sz;
    return kal_ok;
}
kal_uintptr kal_task_parallelism(void) {
    int n = 0; unsigned long len = sizeof n;
    return !okf::failed(okf::sysctlbyname("hw.ncpu", &n, &len)) && n > 0 ? static_cast<kal_uintptr>(n) : 0;
}
kal_uintptr kal_task_cores(void) { return 0; }
int kal_task_set_priority(kal_uintptr pr) {
    if (pr > KAL_TASK_PRIORITY_BACKGROUND) return kal_err_invalid;
    const int nice_v = pr == KAL_TASK_PRIORITY_DEFAULT ? 0 : pr == KAL_TASK_PRIORITY_LOW ? 10 : 19;
    const word r = okf::setpriority(PRIO_PROCESS, 0, nice_v);
    return okf::failed(r) ? okf::fail(r) : kal_ok;
}
// THE KERNEL'S WAIT ON A WORD. It returns at once where the word no longer
// holds `expected'; a bound is relative, and its expiry is kal_err_again.
int kal_task_wait(const kal_u32* word_, kal_u32 expected, kal_u64 timeout_ns) {
    if (!word_) return kal_err_invalid;
    void* obj = const_cast<kal_u32*>(word_);
    unsigned* count = waiting_on(word_);
    __atomic_add_fetch(count, 1u, __ATOMIC_SEQ_CST);
    word r;
    if (timeout_ns == 0) {
        r = okf::umtx(obj, UMTX_OP_WAIT_UINT_PRIVATE, expected, nullptr, nullptr);
    } else {
        struct timespec t{ static_cast<time_t>(timeout_ns / 1000000000ull), static_cast<long>(timeout_ns % 1000000000ull) };
        r = okf::umtx(obj, UMTX_OP_WAIT_UINT_PRIVATE, expected, nullptr, &t);
    }
    __atomic_sub_fetch(count, 1u, __ATOMIC_SEQ_CST);
    if (r == -ETIMEDOUT) return kal_err_again;
    return kal_ok;   // woken, changed, or interrupted: the caller re-examines
}
int kal_task_wake(const kal_u32* word_, kal_uintptr count, kal_uintptr* woken) {
    if (!word_) return kal_err_invalid;
    const int n = count > 0x7fffffff ? 0x7fffffff : static_cast<int>(count);
    const unsigned waiting = __atomic_load_n(waiting_on(word_), __ATOMIC_SEQ_CST);
    const word r = n ? okf::umtx(const_cast<kal_u32*>(word_), UMTX_OP_WAKE_PRIVATE, static_cast<unsigned long>(n), nullptr, nullptr) : 0;
    if (okf::failed(r)) return okf::fail(r);
    if (woken) *woken = count < waiting ? count : waiting;
    return kal_ok;
}
kal_uintptr kal_task_props(void) {
    return KAL_TASK_PROP_PREEMPTIVE | KAL_TASK_PROP_PARALLEL | KAL_TASK_PROP_WAIT_TIMEOUT
         | KAL_TASK_PROP_THREAD_LOCAL | KAL_TASK_PROP_PRIORITY;
}

}  // extern "C"
