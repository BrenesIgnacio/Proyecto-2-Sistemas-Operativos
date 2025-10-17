#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define PAGE_SIZE 4096
#define RAM_FRAMES 100

typedef uint32_t sim_pid_t;
typedef uint32_t sim_ptr_t;
typedef uint32_t sim_pageid_t;
typedef uint64_t sim_time_t;

#endif