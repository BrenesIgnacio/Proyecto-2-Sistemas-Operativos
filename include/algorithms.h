#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include "sim_types.h"

sim_pageid_t fifo_choose_victim(Simulator *sim);
sim_pageid_t sc_choose_victim(Simulator *sim);
sim_pageid_t mru_choose_victim(Simulator *sim);
sim_pageid_t rnd_choose_victim(Simulator *sim);
sim_pageid_t opt_choose_victim(Simulator *sim);

#endif
