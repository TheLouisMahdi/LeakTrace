#define LT_PACKED 1
#define LT_IMPLEMENTATION
#include "../LeakTrace.h"
void lt_smoke(void) {
    lt_init();
    lt_on_alloc_at((void*)0x24000400u, 32u, 0x08010101u, 12u);
    lt_poll_at(20u, 256000u, 128000u, 100000u);
}
