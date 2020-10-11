#pragma once

#define BILLION 1000000000UL
#define LOG_TIMEOUT 1000
// logging helper
#define PGSIZE 512
#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define ATOM_FETCH_ADD(dest, value) \
        dest += value;
