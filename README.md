# futex-event
`futex-event` is a library to meant to emulate Win32 events especially `WaitForMultipleObjects`.

Powered by `futex2` API with `futex_waitv` and `io_uring_prep_futex_wait` for better WFMO supports.

## About futex-event
Inspired by [pevents](https://github.com/neosmart/pevents), I decided to implement similar things on top of futex.
> Why futex and are you reinvent the wheel?

`cnd_t` on Linux is 48 bytes while the integer I use for futex is 32 bytes.
By functionality, yes. It has similar mechanism

But internally, no. `WaitForMultipleObjects` is a system call only Windows `NtWaitForMultipleObjects` and not emulated in userspace.

With `futex_waitv` we can emulate `WaitAny` efficiently and `io_uring_prep_futex_wait` can emulate `WaitAll` without system call overhead.

futex didn't use any fd so the kernel didn't need to maintain and less system call for creating and destroying.

## futex-event API
The futex-event API is modeled almost identically with Win32 Events and its wait except you need to pass mutex for protectting the event.

It's free from spurious wake up.

**The problem is `io_uring_submit_and_wait_timeout` use relative timeout but spurious wake up may happen with futex. In order to address that, I simulate absolute timeout with subtracting into current timepoint whenever it needs to wait again**

```cpp
int InitializeEvent(futex_event_t *event, bool manualReset, bool initialState);
int SetEvent(mtx_t *mutex, futex_event_t *event);
int ResetEvent(mtx_t *mutex, futex_event_t *event);
int WaitForSingleEvent(mtx_t *mutex, futex_event_t *event, uint64_t milliseconds);
int WaitForMultipleEvents(mtx_t *mutex, size_t count, futex_event_t *events, bool WaitAll, uint64_t milliseconds);
```
