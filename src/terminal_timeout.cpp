// openkal.terminal (the tty's ioctls) and openkal.timeout (ppoll, and a
// program awaited with a bound).
#include "common.h"

namespace {
using okf::word;

int ready(int fd, short ev, kal_u64 ns) {
    struct pollfd p{ fd, ev, 0 };
    struct timespec t{ static_cast<time_t>(ns / 1000000000ull), static_cast<long>(ns % 1000000000ull) };
    for (;;) {
        const word r = okf::ppoll(&p, 1, ns == 0 ? nullptr : &t, nullptr);
        if (r == -EINTR) continue;
        if (okf::failed(r)) return okf::fail(r);
        return r > 0 ? kal_ok : kal_err_again;
    }
}
}  // namespace

extern "C" {

// ================================================================ terminal
int kal_terminal_get_mode(kal_stream s, kal_uintptr* mode) {
    if (!mode) return kal_err_invalid;
    struct termios t{};
    const word r = okf::ioctl(okf::unpack(s.h), TIOCGETA, &t);
    if (okf::failed(r)) return r == -ENOTTY ? kal_err_not_supported : okf::fail(r);
    kal_uintptr m = KAL_TERM_SEQUENCES;
    if (t.c_lflag & ICANON) m |= KAL_TERM_LINE_EDIT;
    if (t.c_lflag & ECHO)   m |= KAL_TERM_ECHO;
    if (!(t.c_lflag & ISIG) && !(t.c_iflag & IXON)) m |= KAL_TERM_PASS_CONTROL;
    if (!(t.c_iflag & (ICRNL | INLCR | IGNCR | ISTRIP))) m |= KAL_TERM_PASS_INPUT;
    *mode = m;
    return kal_ok;
}
int kal_terminal_set_mode(kal_stream s, kal_uintptr m) {
    const int fd = okf::unpack(s.h);
    struct termios t{};
    const word r = okf::ioctl(fd, TIOCGETA, &t);
    if (okf::failed(r)) return r == -ENOTTY ? kal_err_not_supported : okf::fail(r);
    auto setb = [](tcflag_t& w, tcflag_t b, bool on) { w = on ? (w | b) : (w & ~b); };
    setb(t.c_lflag, ICANON, m & KAL_TERM_LINE_EDIT);
    setb(t.c_lflag, ECHO,   m & KAL_TERM_ECHO);
    setb(t.c_lflag, ISIG | IEXTEN, !(m & KAL_TERM_PASS_CONTROL));
    setb(t.c_iflag, IXON,   !(m & KAL_TERM_PASS_CONTROL));
    setb(t.c_iflag, ICRNL | INLCR | IGNCR | ISTRIP, !(m & KAL_TERM_PASS_INPUT));
    if (!(m & KAL_TERM_LINE_EDIT)) { t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0; }
    const word w = okf::ioctl(fd, TIOCSETAF, &t);
    return okf::failed(w) ? okf::fail(w) : kal_ok;
}
int kal_terminal_size(kal_stream s, kal_uintptr* cols, kal_uintptr* rows) {
    struct winsize w{};
    if (okf::failed(okf::ioctl(okf::unpack(s.h), TIOCGWINSZ, &w)) || w.ws_col == 0) return kal_err_not_supported;
    if (cols) *cols = w.ws_col;
    if (rows) *rows = w.ws_row;
    return kal_ok;
}
// The mode where the stream is a terminal; its size where the terminal states
// one (a serial line states none).
kal_uintptr kal_terminal_props(kal_stream s) {
    const int fd = okf::unpack(s.h);
    struct termios t{};
    if (fd < 0 || okf::failed(okf::ioctl(fd, TIOCGETA, &t))) return 0;
    struct winsize w{};
    const bool sized = !okf::failed(okf::ioctl(fd, TIOCGWINSZ, &w)) && w.ws_col != 0;
    return KAL_TERM_PROP_MODE | (sized ? KAL_TERM_PROP_SIZE : 0);
}

// ================================================================ timeout
kal_intptr kal_timeout_read(kal_stream s, void* buf, kal_uintptr len, kal_u64 ns) {
    const int r = ready(okf::unpack(s.h), POLLIN, ns);
    if (r != kal_ok) return -static_cast<kal_intptr>(r);
    return kal_stream_read(s, buf, len);
}
kal_intptr kal_timeout_write(kal_stream s, const void* buf, kal_uintptr len, kal_u64 ns) {
    const int r = ready(okf::unpack(s.h), POLLOUT, ns);
    if (r != kal_ok) return -static_cast<kal_intptr>(r);
    return kal_stream_write(s, buf, len);
}
int kal_timeout_wait_process(kal_process p, kal_u64 ns, int* status, int* terminated) {
    int pid = 0;
    const int e = okf::started_pid(p, &pid);
    if (e != kal_ok) return e;
    const kal_u64 end = kal_time_monotonic() + ns;
    for (;;) {
        bool done = false;
        const int r = okf::reap_program(pid, WNOHANG, status, terminated, &done);
        if (r != kal_ok || done) return r;
        if (ns != 0 && kal_time_monotonic() >= end) return kal_err_again;
        kal_time_sleep(1000000);   // a millisecond, the granularity stated below
    }
}
kal_u64 kal_timeout_granularity(void) { return 1000000; }
// The network's resources are not provided (openkal.net and openkal.datagram
// are absent), and timeout.h states that the operations of a resource type an
// implementation withholds report kal_err_not_supported.
int kal_timeout_accept(kal_net_listener, kal_u64, kal_net_conn*) { return kal_err_not_supported; }
kal_intptr kal_timeout_recv_from(kal_datagram, void*, kal_uintptr, kal_endpoint*, kal_u64) {
    return -kal_err_not_supported;
}

}  // extern "C"
