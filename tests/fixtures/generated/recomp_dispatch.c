#include <stdint.h>
#include <stddef.h>

typedef void (*recomp_func_t)(void);

void sub_0006EE30(void);

int recomp_dispatch_init(void)
{
    return 1;
}

recomp_func_t recomp_lookup(uint32_t address)
{
    return address == 0x0006EE30u ? sub_0006EE30 : NULL;
}
