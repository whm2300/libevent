/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _EVENT_INTERNAL_H_
#define _EVENT_INTERNAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "event2/event-config.h"
#include <time.h>
#include <sys/queue.h>
#include "event2/event_struct.h"
#include "minheap-internal.h"
#include "evsignal-internal.h"
#include "mm-internal.h"
#include "defer-internal.h"

/* map union members back */

/* mutually exclusive */
#define ev_signal_next	_ev.ev_signal.ev_signal_next
#define ev_io_next	_ev.ev_io.ev_io_next
#define ev_io_timeout	_ev.ev_io.ev_timeout

/* used only by signals */
#define ev_ncalls	_ev.ev_signal.ev_ncalls
#define ev_pncalls	_ev.ev_signal.ev_pncalls

/* Possible values for ev_closure in struct event. */
#define EV_CLOSURE_NONE 0
#define EV_CLOSURE_SIGNAL 1
#define EV_CLOSURE_PERSIST 2

/** 定义后端使用接口及数据结构，改结构体总是指向libevnt定义的epoll select poll 等全局常量 */
struct eventop {
	/** 使用后端名称，epoll select ... */
	const char *name;
	/** 初始化后端函数。 It should
	 * create a new structure holding whatever information is needed to
	 * run the backend, and return it.  The returned pointer will get
	 * stored by event_init into the event_base.evbase field.  On failure,
	 * this function should return NULL. */
	void *(*init)(struct event_base *);
	/** 增加监听事件 
	 * Enable reading/writing on a given fd or signal.  'events' will be
	 * the events that we're trying to enable: one or more of EV_READ,
	 * EV_WRITE, EV_SIGNAL, and EV_ET.  'old' will be those events that
	 * were enabled on this fd previously.  'fdinfo' will be a structure
	 * associated with the fd by the evmap; its size is defined by the
	 * fdinfo field below.  It will be set to 0 the first time the fd is
	 * added.  The function should return 0 on success and -1 on error.
	 */
	int (*add)(struct event_base *, evutil_socket_t fd, short old, short events, void *fdinfo);
	/** 移除指定事件 */
	int (*del)(struct event_base *, evutil_socket_t fd, short old, short events, void *fdinfo);
	/** 开始监听， epoll_wait
	    Function to implement the core of an event loop.  It must see which
	    added events are ready, and cause event_active to be called for each
	    active event (usually via event_io_active or such).  It should
	    return 0 on success and -1 on error.
	 */
	int (*dispatch)(struct event_base *, struct timeval *);
	/** 清理后端结构使用的所有资源 */
	void (*dealloc)(struct event_base *);
	/** Flag: set if we need to reinitialize the event base after we fork.
	 */
	 /* 是否需要初始化 */
	int need_reinit;
	/** 后端能提供的特征 */
	enum event_method_feature features;
	/** Length of the extra information we should record for each fd that
	    has one or more active events.  This information is recorded
	    as part of the evmap entry for each fd, and passed as an argument
	    to the add and del functions above.
	 */
	size_t fdinfo_len;
};

#ifdef WIN32
/* If we're on win32, then file descriptors are not nice low densely packed
   integers.  Instead, they are pointer-like windows handles, and we want to
   use a hashtable instead of an array to map fds to events.
*/
#define EVMAP_USE_HT
#endif

/* #define HT_CACHE_HASH_VALS */

#ifdef EVMAP_USE_HT
#include "ht-internal.h"
struct event_map_entry;
HT_HEAD(event_io_map, event_map_entry);
#else
#define event_io_map event_signal_map
#endif

/* Used to map signal numbers to a list of events.  If EVMAP_USE_HT is not
   defined, this structure is also used as event_io_map, which maps fds to a
   list of events.
*/
struct event_signal_map {
	/* An array of evmap_io * or of evmap_signal *; empty entries are
	 * set to NULL. */
	void **entries;
	/* The number of entries available in entries */
	int nentries;
};

/* A list of events waiting on a given 'common' timeout value.  Ordinarily,
 * events waiting for a timeout wait on a minheap.  Sometimes, however, a
 * queue can be faster.
 **/
struct common_timeout_list {
	/* 超时event队列，所有具有相同超时间隔的超时事件。 */
	struct event_list events;
	/* 超时时长 */
	struct timeval duration;
	/* 超时event代表，放入event_base进行监听。 */
	struct event timeout_event;
	/* The event_base that this timeout list is part of */
	struct event_base *base;
};

/** Mask used to get the real tv_usec value from a common timeout. */
#define COMMON_TIMEOUT_MICROSECONDS_MASK       0x000fffff

struct event_change;

/* List of 'changes' since the last call to eventop.dispatch.  Only maintained
 * if the backend is using changesets. */
struct event_changelist {
	struct event_change *changes;
	int n_changes;
	int changes_size;
};

#ifndef _EVENT_DISABLE_DEBUG_MODE
/* Global internal flag: set to one if debug mode is on. */
extern int _event_debug_mode_on;
#define EVENT_DEBUG_MODE_IS_ON() (_event_debug_mode_on)
#else
#define EVENT_DEBUG_MODE_IS_ON() (0)
#endif

struct event_base {
	/** 具体使用后端结构体指向全局变量eventops某个元素，epoll后端可能指向epollops_changelist */
	const struct eventop *evsel;
	/** 后端定义的数据结构，如epoll指向 struct epollop. */
	void *evbase;

	/** List of changes to tell backend about at next dispatch.  Only used
	 * by the O(1) backends. */
	struct event_changelist changelist;

	/** 信号操作函数 */
	const struct eventop *evsigsel;
	/** Socketpair封装，用于信号实现。 */
	struct evsig_info sig;

	/** Number of virtual events */
	int virtual_event_count;
	/** Number of total events added to this event_base */
	int event_count;
	/** Number of total events active in this event_base */
	int event_count_active;

	/** Set if we should terminate the loop once we're done processing
	 * events. */
	int event_gotterm;
	/** Set if we should terminate the loop immediately */
	int event_break;
	/** Set if we should start a new instance of the loop immediately. */
	int event_continue;

	/** The currently running priority of events */
	int event_running_priority;

	/** 是否继续循环. */
	int running_loop;

	/* 激活事件队列，下标为优先级。 */
	/** An array of nactivequeues queues for active events (ones that
	 * have triggered, and whose callbacks need to be called).  Low
	 * priority numbers are more important, and stall higher ones.
	 */
	struct event_list *activequeues;
	/** The length of the activequeues array */
	int nactivequeues;

	/* common timeout logic */

	/** 公用超时数组，每个数组元素对应不同的超时间隔。 */
	struct common_timeout_list **common_timeout_queues;
	/** 已用元素个数 */
	int n_common_timeouts;
	/** 总长度*/
	int n_common_timeouts_allocated;

	/** List of defered_cb that are active.  We run these after the active
	 * events. */
	struct deferred_cb_queue defer_queue;

	/** 所有监听的IO事件 */
	struct event_io_map io;

	/** 所有监听的信号事件. */
	struct event_signal_map sigmap;

	/** All events that have been enabled (added) in this event_base */
	struct event_list eventqueue;

	/** Stored timeval; used to detect when time is running backwards. */
	struct timeval event_tv;

	/** 时间堆. */
	struct min_heap timeheap;

	/** 时间缓存: used to avoid calling gettimeofday/clock_gettime
	 * too often. */
	struct timeval tv_cache;

#if defined(_EVENT_HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
	/** Difference between internal time (maybe from clock_gettime) and
	 * gettimeofday. */
	struct timeval tv_clock_diff;
	/** Second in which we last updated tv_clock_diff, in monotonic time. */
	time_t last_updated_clock_diff;
#endif

#ifndef _EVENT_DISABLE_THREAD_SUPPORT
	/* threading support */
	/** The thread currently running the event_loop for this base */
	unsigned long th_owner_id;
	/** A lock to prevent conflicting accesses to this event_base */
	void *th_base_lock;
	/** The event whose callback is executing right now */
	struct event *current_event;
	/** A condition that gets signalled when we're done processing an
	 * event with waiters on it. */
	void *current_event_cond;
	/** Number of threads blocking on current_event_cond. */
	int current_event_waiters;
#endif

#ifdef WIN32
	/** IOCP support structure, if IOCP is enabled. */
	struct event_iocp_port *iocp;
#endif

	/** Flags that this base was configured with */
	enum event_base_config_flag flags;

	/* Notify main thread to wake up break, etc. */
	/** True if the base already has a pending notify, and we don't need
	 * to add any more. */
	int is_notify_pending;
	/** Socketpair 用于唤醒当前线程 */
	evutil_socket_t th_notify_fd[2];
	/** An event used by some th_notify functions to wake up the main
	 * thread. */
	struct event th_notify;
	/** A function used to wake up the main thread from another thread. */
	int (*th_notify_fn)(struct event_base *base);
};

struct event_config_entry {
	TAILQ_ENTRY(event_config_entry) next;

	const char *avoid_method;
};

/** Internal structure: describes the configuration we want for an event_base
 * that we're about to allocate. */
struct event_config {
	TAILQ_HEAD(event_configq, event_config_entry) entries;

	int n_cpus_hint;
	enum event_method_feature require_features;
	enum event_base_config_flag flags;
};

/* Internal use only: Functions that might be missing from <sys/queue.h> */
#if defined(_EVENT_HAVE_SYS_QUEUE_H) && !defined(_EVENT_HAVE_TAILQFOREACH)
#ifndef TAILQ_FIRST
#define	TAILQ_FIRST(head)		((head)->tqh_first)
#endif
#ifndef TAILQ_END
#define	TAILQ_END(head)			NULL
#endif
#ifndef TAILQ_NEXT
#define	TAILQ_NEXT(elm, field)		((elm)->field.tqe_next)
#endif

#ifndef TAILQ_FOREACH
#define TAILQ_FOREACH(var, head, field)					\
	for ((var) = TAILQ_FIRST(head);					\
	     (var) != TAILQ_END(head);					\
	     (var) = TAILQ_NEXT(var, field))
#endif

#ifndef TAILQ_INSERT_BEFORE
#define	TAILQ_INSERT_BEFORE(listelm, elm, field) do {			\
	(elm)->field.tqe_prev = (listelm)->field.tqe_prev;		\
	(elm)->field.tqe_next = (listelm);				\
	*(listelm)->field.tqe_prev = (elm);				\
	(listelm)->field.tqe_prev = &(elm)->field.tqe_next;		\
} while (0)
#endif
#endif /* TAILQ_FOREACH */

#define N_ACTIVE_CALLBACKS(base)					\
	((base)->event_count_active + (base)->defer_queue.active_count)

int _evsig_set_handler(struct event_base *base, int evsignal,
			  void (*fn)(int));
int _evsig_restore_handler(struct event_base *base, int evsignal);


void event_active_nolock(struct event *ev, int res, short count);

/* FIXME document. */
void event_base_add_virtual(struct event_base *base);
void event_base_del_virtual(struct event_base *base);

/** For debugging: unless assertions are disabled, verify the referential
    integrity of the internal data structures of 'base'.  This operation can
    be expensive.

    Returns on success; aborts on failure.
*/
void event_base_assert_ok(struct event_base *base);

#ifdef __cplusplus
}
#endif

#endif /* _EVENT_INTERNAL_H_ */

