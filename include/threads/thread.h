#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"		//system call 추가
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */



	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */
	//mlfqs 스케줄러에 사용할 배열
	//struct list_elem elem;
	struct list_elem all_elem;

	//mlfqs에 필요한 nice변수와 최근 cpu사용 쓰레드.
	int nice;
	int recent_cpu;



	//lock걸기
	// 현재 thread에 donation 해주는 thread list
	// donation에 대한 현재 thread를 원소

	int init_priority;
	
	struct lock *wait_on_lock;
	struct list donations;
	struct list_elem donation_elem;
	

//system call에 사용하기 위해서 블럭 주석처리
//#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
//#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
	int64_t wakeup; // 깨어나야 하는 ticks 값

	//system call 수정사항
	struct list child_list;
	struct list_elem chlid_elem;
	struct semaphore wait_sema;		//자식 프로세스가 종료할때까지 대기함

	int exit_status;

	struct intr_frame parent_if; 
	struct semaphore fork_sema;				//fork 완료 할때까지 부모가 기다리게 함
	struct semaphore free_sema; 			//자식 종료할 때까지 종료 대기


	//system call file descriptor
	struct file **fd_table;
	int fd_idx;
	
	struct file *running;

	//vm
	uintptr_t user_rsp;
	struct list mmap_list;
	




};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;


void thread_init (void);
void thread_start (void);

void thread_sleep(int64_t ticks);	//sleep awake method 추가
void thread_awake(int64_t ticks);


void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

//우선순위 비교 함수
bool thread_compare_priority(const struct list_elem*, const struct list_elem*, void *aux UNUSED);
//donation 우선순위 비교 함수
bool thread_compare_donate_priority (const struct list_elem*, const struct list_elem*, void *aux UNUSED);
//donate 우선 순위
void donate_priority(void);

void remove_lock(struct lock*);

void rebuild_priority(void);

void thread_preemption();

//mlfqs 관련 methods
void mlfqs_priority(struct thread *t);
void mlfqs_recent_cpu(struct thread *t);
void mlfqs_load_avg(void);
void mlfqs_increment(void);
void mlfqs_recalc(void);


//system call 
#define FDT_PAGES 3
#define FDCOUNT_LIMIT FDT_PAGES * (1<<9)

#endif /* threads/thread.h */
