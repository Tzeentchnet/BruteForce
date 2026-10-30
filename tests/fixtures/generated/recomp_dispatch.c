#include <stdint.h>
#include <stddef.h>

typedef void (*recomp_func_t)(void);

int recomp_dispatch_init(void)
{
    return 1;
}

recomp_func_t recomp_lookup(uint32_t address)
{
    (void)address;
    return NULL;
}
