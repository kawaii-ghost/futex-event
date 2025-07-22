#define _GNU_SOURCE
/*
 * Win32 Events on top futex
 * Author: KawaiiGhost <ntwritefile@duck.com>
 * Copyright (C) 2025 by KawaiiGhost
 * SPDX-License-Identifier: MIT
 */

#include "event_futex.h"
#include <stdbool.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <threads.h>
#include <liburing.h>
#include <stdio.h>

#if __STDC__VERSION < 202311L
#define nullptr NULL
#endif

int futex_wait(void *addr, uint64_t val, uint64_t mask, uint32_t flags, struct timespec *timeout, clockid_t clockid)
{
        return syscall(SYS_futex_wait, addr, val, mask, flags, timeout, clockid);
}

int futex_wake(void *addr, uint64_t mask, uint32_t nr, uint32_t flags)
{
        return syscall(SYS_futex_wake, addr, mask, nr, flags);
}

int futex_waitv(struct futex_waitv *waiter, uint32_t nr, uint32_t flags, struct timespec *timeout, clockid_t clockid)
{
        return syscall(SYS_futex_waitv, waiter, nr, flags, timeout, clockid);
}

struct timespec timespec_normalise(struct timespec ts)
{
    imaxdiv_t div_result = imaxdiv(ts.tv_nsec, NSEC_PER_SEC);

    ts.tv_sec += div_result.quot;
    ts.tv_nsec = div_result.rem;

    if (ts.tv_nsec < 0) {
        ts.tv_sec--;
        ts.tv_nsec += NSEC_PER_SEC;
    }

    return ts;
}

struct timespec timespec_sub(struct timespec ts1, struct timespec ts2)
{
	/* Normalise inputs to prevent tv_nsec rollover if whole-second values
	 * are packed in it.
	*/
	ts1 = timespec_normalise(ts1);
	ts2 = timespec_normalise(ts2);
	
	ts1.tv_sec  -= ts2.tv_sec;
	ts1.tv_nsec -= ts2.tv_nsec;
	
	return timespec_normalise(ts1);
}

int InitializeEvent(futex_event_t *event, bool manualReset, bool initialState)
{
        if (event == nullptr) {
                return EINVAL;
        }

        event->manual = manualReset;
        event->signal = initialState;
        return 0;
}

int SetEvent(mtx_t *mutex, futex_event_t *event)
{
        if (mutex == nullptr || event == nullptr ) {
                return EINVAL;
        }

        mtx_lock(mutex);
        if (event->signal == SIGNALED) {
                mtx_unlock(mutex);
                return 0;
        }

        int nr_wake;
        if (event->manual == true) {
                nr_wake = INT32_MAX;
        } else {
                nr_wake = 1;
        }
        
        event->signal = SIGNALED;
        int ret = futex_wake(&event->signal, FUTEX_BITSET_MATCH_ANY, nr_wake, FUTEX2_PRIVATE | FUTEX2_SIZE_U32);
        mtx_unlock(mutex);
        return ret == 0 ? 0 : errno;
}

int ResetEvent(mtx_t *mutex, futex_event_t *event)
{
        if (mutex == nullptr || event == nullptr) {
                return EINVAL;
        }

        mtx_lock(mutex);
        event->signal = UNSIGNALED;
        mtx_unlock(mutex);
        return 0;
}

int WaitForSingleEvent(mtx_t *mutex, futex_event_t *event, uint64_t milliseconds)
{
        if (mutex == nullptr || event == nullptr) {
                return EINVAL;
        }

        struct timespec *out, point;
        if (milliseconds == INFINITE) {
                puts("yes");
                out = nullptr;
        } else {
                timespec_get(&point, TIME_UTC);
                imaxdiv_t res = imaxdiv(milliseconds, 1000);
                point.tv_nsec += res.rem * 1000000LL;
                point.tv_sec += res.quot;
                point = timespec_normalise(point);
                out = &point;
        }

        mtx_lock(mutex);
        while (1) {
                if (event->signal == SIGNALED) {
                        if (event->manual == false) {
                                event->signal = UNSIGNALED;
                                mtx_unlock(mutex);
                                return 0;
                        } else {
                                mtx_unlock(mutex);
                                return 0;
                        }
                } else {
                        mtx_unlock(mutex);
                        int ret = futex_wait(&event->signal, UNSIGNALED, FUTEX_BITSET_MATCH_ANY, FUTEX2_PRIVATE | FUTEX2_SIZE_U32, out, CLOCK_REALTIME);
                        mtx_lock(mutex);
                        if (ret == -1 && errno == ETIMEDOUT) {
                                mtx_unlock(mutex);
                                return ETIMEDOUT;
                        } else {
                                break;
                        }
                }
        }
        mtx_unlock(mutex);
        return 0;
}

int WaitForMultipleEvents(mtx_t *mutex, size_t count, futex_event_t *events, bool waitAll, uint64_t milliseconds)
{
        if (count > MAXIMUM_WAIT_EVENTS || count == 0 || events == nullptr || mutex == nullptr) {
                return EINVAL;
        }

        struct timespec *out = nullptr, point;
        struct __kernel_timespec kts;
        if (milliseconds != INFINITE) {
                timespec_get(&point, TIME_UTC);
                imaxdiv_t res = imaxdiv(milliseconds, 1000);
                point.tv_nsec += res.rem * 1000000LL;
                point.tv_sec += res.quot;
                point = timespec_normalise(point);
                kts.tv_sec = point.tv_sec;
                kts.tv_nsec = point.tv_nsec;
                out = (struct timespec *)&kts;
        }

        if (waitAll == false) {
                struct futex_waitv vec[MAXIMUM_WAIT_EVENTS];
                for (size_t i = 0; i < count; i++) {
                        vec[i].val = UNSIGNALED;
                        vec[i].uaddr = (uint64_t)&events[i].signal;
                        vec[i].flags = FUTEX2_PRIVATE | FUTEX2_SIZE_U32;
                        vec[i].__reserved = 0;
                }

                while (1) {
                        mtx_lock(mutex);
                        for (size_t i = 0; i < count; i++) {
                                if (events[i].signal == SIGNALED) {
                                        if (events[i].manual == false) {
                                                events[i].signal = UNSIGNALED;
                                        }
                                        mtx_unlock(mutex);
                                        return i;
                                }
                        }
                        mtx_unlock(mutex);
                        int ret = futex_waitv(vec, count, 0, out, CLOCK_REALTIME);
                        if (ret == -1) {
                                if (errno == ETIMEDOUT) {
                                        return ETIMEDOUT;
                                }
                                // We back into the loop again if EAGAIN
                        } else {
                                mtx_lock(mutex);
                                if (events[ret].signal == SIGNALED) {
                                        if (events[ret].manual == false) {
                                                events[ret].signal = UNSIGNALED;
                                        }
                                        mtx_unlock(mutex);
                                        return ret;
                                }
                                mtx_unlock(mutex);
                        }
                }
        } else {
                struct io_uring ring;
                struct io_uring_sqe *sqe;
                struct io_uring_cqe *cqe;
                int ret = io_uring_queue_init(count, &ring, IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN);
                if (ret < 0) {
                        return -ret;
                }

                mtx_lock(mutex);
                while (1) {
                        bool all_signaled = true;
                        for (size_t i = 0; i < count; i++) {
                                if (events[i].signal == UNSIGNALED) {
                                        all_signaled = false;
                                        break;
                                }
                        }

                        if (all_signaled == true) {
                                for (size_t i = 0; i < count; i++) {
                                        if (events[i].manual == false) {
                                                events[i].signal = UNSIGNALED;
                                        }
                                }
                                mtx_unlock(mutex);
                                io_uring_queue_exit(&ring);
                                return 0;
                        }

                        for (size_t i = 0; i < count; i++) {
                                sqe = io_uring_get_sqe(&ring);
                                if (sqe == nullptr) {
                                        io_uring_queue_exit(&ring);
                                        mtx_unlock(mutex);
                                        return EINVAL;
                                }
                                io_uring_prep_futex_wait(sqe, (uint32_t *)&events[i].signal, UNSIGNALED, FUTEX_BITSET_MATCH_ANY, FUTEX2_PRIVATE | FUTEX2_SIZE_U32, 0);
                        }

                        if (milliseconds != INFINITE) {
                                struct timespec now;
                                timespec_get(&now, TIME_UTC);
                                struct timespec remaining = timespec_sub(point, now);
                                if (remaining.tv_sec <= 0) {
                                        io_uring_queue_exit(&ring);
                                        mtx_unlock(mutex);
                                        return ETIMEDOUT;
                                }
                                kts.tv_sec = remaining.tv_sec;
                                kts.tv_nsec = remaining.tv_nsec;
                                out = (struct timespec *)&kts;
                        }

                        mtx_unlock(mutex);
                        ret = io_uring_submit_and_wait_timeout(&ring, &cqe, count, (struct __kernel_timespec *)out, nullptr);
                        mtx_lock(mutex);
                        if (io_uring_cq_ready(&ring) < count) {
                                io_uring_queue_exit(&ring);
                                mtx_unlock(mutex);
                                return ETIMEDOUT;
                        }
                }
        }
}
