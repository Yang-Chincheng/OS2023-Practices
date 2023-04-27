/* YOUR CODE HERE */
#ifndef COROUTINE_H
#define COROUTINE_H

#define _GNU_SOURCE
#define _POSIX_SOURCE
#define _XOPEN_SOURCE 600 // for pthread rwlock

#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <ucontext.h>

#undef _GNU_SOURCE
#undef _POSIX_SOURCE
#undef _XOPEN_SOURCE

typedef long long cid_t;
#define MAXN (50000)
#define UNAUTHORIZED (-1)
#define FINISHED (2)
#define RUNNING (1)

int co_start(int (*routine)(void));
int co_getid();
int co_getret(int cid);
int co_yield();
int co_waitall();
int co_wait(int cid);
int co_status(int cid);

#endif
