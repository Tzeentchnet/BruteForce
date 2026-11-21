#include <string.h>

#include "combiner_state.h"

void gpu_combiner_default(int texture0, Nv2aCombiner *combiner)
{
    memset(combiner, 0, sizeof *combiner);
    combiner->control = 1;
    combiner->stage_program = texture0 ? 1 : 0;
    combiner->color_icw[0] = (texture0 ? 0x08u : 0x20u) << 24 | 0x04u << 16;
    combiner->alpha_icw[0] = (texture0 ? 0x18u : 0x30u) << 24 | 0x14u << 16;
    combiner->color_ocw[0] = 0x0Cu << 4;
    combiner->alpha_ocw[0] = 0x0Cu << 4;
}
