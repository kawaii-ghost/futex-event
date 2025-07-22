/*
 * Win32 Events on top futex
 * Author: KawaiiGhost <ntwritefile@duck.com>
 * Copyright (C) 2025 by KawaiiGhost
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <threads.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <stdbool.h>
#include <linux/futex.h>

int futex_wait(void *addr, uint64_t val, uint64_t mask, uint32_t flags, struct timespec *timeout, clockid_t clockid);
int futex_waitv(struct futex_waitv *waiter, uint32_t nr, uint32_t flags, struct timespec *timeout, clockid_t clockid);
int futex_wake(void *addr, uint64_t mask, uint32_t nr, uint32_t flags);

#define NSEC_PER_SEC 1000000000

struct timespec timespec_normalise(struct timespec ts);
struct timespec timespec_sub(struct timespec s1, struct timespec s2);

#define UNSIGNALED 0
#define SIGNALED 1

#define INFINITE UINT64_MAX
#define MAXIMUM_WAIT_EVENTS 64

typedef struct _futex_event_t {
        uint32_t signal;
        bool manual;
} futex_event_t;

int InitializeEvent(futex_event_t *event, bool manualRest, bool initialState);
int SetEvent(mtx_t *mutex, futex_event_t *event);
int ResetEvent(mtx_t *mutex, futex_event_t *event);
int WaitForSingleEvent(mtx_t *mutex, futex_event_t *event, uint64_t milliseconds);
int WaitForMultipleEvents(mtx_t *mutex, size_t count, futex_event_t *events, bool WaitAll, uint64_t milliseconds);
