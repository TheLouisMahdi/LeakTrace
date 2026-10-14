#include <cassert>
#include <new>
#define LT_MAX_LIVE 32u
#define LT_BUCKETS 16u
#define LT_FAST_SLOTS 4u
#define LT_SLOW_SLOTS 4u
#define LT_IMPLEMENTATION
#include "../LeakTrace.h"
int main() {
    lt_init();
    /* Direct calls exercise the adapter: C++ may legally elide a paired
     * new-expression/delete-expression even when global new is replaced. */
    void *p = ::operator new(64u);
    lt_stats_t s{};
    assert(p && lt_get_stats(&s) && s.live_count == 1u && s.live_traced_bytes == 64u);
    ::operator delete(p);
    assert(lt_get_stats(&s) && s.live_count == 0u && s.free_calls == 1u);
    p = ::operator new[](128u, std::nothrow);
    assert(p && lt_get_stats(&s) && s.live_count == 1u && s.live_traced_bytes == 128u);
    ::operator delete[](p, std::nothrow);
    p = ::operator new(32u, std::nothrow);
    assert(p && lt_get_stats(&s) && s.live_count == 1u);
    ::operator delete(p, std::nothrow);
    p = ::operator new[](16u);
    assert(p && lt_get_stats(&s) && s.live_count == 1u);
    ::operator delete[](p);
    assert(lt_get_stats(&s) && s.live_count == 0u && s.free_calls == 4u);
}
