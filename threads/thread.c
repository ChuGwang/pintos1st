#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"



//---------------------------------------------------------
#include "devices/timer.h"
//---------------------------------------------------------



#ifdef USERPROG
#include "userprog/process.h"
#endif



//-----------------------------------------------------------------------------------
// Aging 및 MLFQS 설정값
#define AGE_LIMIT 20
#define MLFQS_SLICE_Q0 2
#define MLFQS_SLICE_Q1 4
#define MLFQS_SLICE_Q2 8

#define min(a,b) (((a) < (b)) ? (a) : (b))
//------------------------------------------------------------------------------------



/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* List of process in sleep */
static struct list sleep_list;
static int64_t next_tick_to_wakeup = INT64_MAX;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame
{
    void *eip;             /* Return address. */
    thread_func *function; /* Function to call. */
    void *aux;             /* Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4          /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */


//-----------------------------------------------------------------------------------------------
bool thread_mlfqs = false;
// 여기 위에 (thread_mlfqs = false) 로 수정
//--------------------------------------------------------------------------------------------------


//----------------------------------------------------------------------------------------
// MLFQS용 준비 큐 (Q0, Q1, Q2)
static struct list mlfqs_ready_q[3];
//----------------------------------------------------------------------------------------


static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void)
{
    ASSERT (intr_get_level () == INTR_OFF);

    lock_init (&tid_lock);
    list_init (&ready_list);
    list_init (&all_list);
    list_init (&sleep_list);


    //-------------------------------------------------------------------
    // -----  MLFQS 큐 초기화 추가 ----- 
     list_init (&mlfqs_ready_q[0]);
     list_init (&mlfqs_ready_q[1]);
     list_init (&mlfqs_ready_q[2]);
    // -----  추가 끝 ----- 
   //---------------------------------------------------------------------
   

    /* Set up a thread structure for the running thread. */
    initial_thread = running_thread ();
    init_thread (initial_thread, "main", PRI_DEFAULT);
    initial_thread->status = THREAD_RUNNING;
    initial_thread->tid = allocate_tid ();
}



//----------------------------------------------------------------------
// ----- 새로운 함수 구현: thread_priority_compare ----- 
//
 // 'a'의 우선순위가 'b'의 우선순위보다 높으면(크면) true를 반환합니다.
 // list_insert_ordered()에 의해 내림차순 정렬에 사용됩니다.

bool
thread_priority_compare(const struct list_elem *a,
                        const struct list_elem *b,
                        void *aux UNUSED)
{
  struct thread *ta = list_entry(a, struct thread, elem);
  struct thread *tb = list_entry(b, struct thread, elem);
  return ta->priority > tb->priority;
}
// -----  새로운 함수 구현 끝 ----- 

//----------------------------------------------------------------------





//------------------------------------------------------------------------
// ----- 새로운 함수 구현: thread_check_preemption ----- 

 // 현재 실행 중인 스레드가 준비 큐의 최고 우선순위 스레드보다
 // 우선순위가 낮은지 확인하고, 낮다면 즉시 양보(yield)합니다.
 // (MLFQS 모드가 아닐 때만 사용됩니다)

void
thread_check_preemption(void)
{
  // MLFQS 모드이거나, 인터럽트 컨텍스트에서는 이 함수를 사용하지 않습니다. 
  if (thread_mlfqs || intr_context())
    return;

  if (!list_empty(&ready_list))
  {
    struct thread *highest_ready = list_entry(list_front(&ready_list), struct thread, elem);
    if (highest_ready->priority > thread_current()->priority)
    {
      thread_yield();
    }
  }
}
// ----- 새로운 함수 구현 끝 ----- 
//-------------------------------------------------------------------------



/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void)
{
    /* Create the idle thread. */
    struct semaphore idle_started;
    sema_init (&idle_started, 0);
    thread_create ("idle", PRI_MIN, idle, &idle_started);

    /* Start preemptive thread scheduling. */
    intr_enable ();

    /* Wait for the idle thread to initialize idle_thread. */
    sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void)
{



    //-----------------------------------------------------------------------
   // 2차 수정
    // -----수정된 부분----- 
    thread_wakeup(timer_ticks()); 
    // ----- 수정 끝 ----- 
   //-----------------------------------------------------------------------


   
    struct thread *t = thread_current ();

    // Update statistics. 
    if (t == idle_thread)
        idle_ticks++;
#ifdef USERPROG
    else if (t->pagedir != NULL)
        user_ticks++;
#endif
    else
        kernel_ticks++;

   // 원본
    // Enforce preemption. 
    /*if (++thread_ticks >= TIME_SLICE)
        intr_yield_on_return ();*/
        



   //-------------------------------------------------------------------------
   //----- 3. 새로운 스케줄링 로직 추가 ----- 

  // 스케줄러 로직은 매 틱마다 실행됩니다. 
  if (thread_mlfqs)
  {
    // ========= MLFQS 스케줄러 로직 ========= 

    // 3-A. 실행 중인 스레드 처리 (Time Slice 소모 및 강등) 
    if (t != idle_thread)
    {
      t->ticks_in_current_slice++;
      int slice_limit = 0;

      if (t->mlfqs_queue_level == 0) slice_limit = MLFQS_SLICE_Q0;
      else if (t->mlfqs_queue_level == 1) slice_limit = MLFQS_SLICE_Q1;
      else slice_limit = MLFQS_SLICE_Q2;

      // 타임 슬라이스를 모두 소진했으면 강등(Demote)시키고 양보(Yield) 
      if (t->ticks_in_current_slice >= slice_limit)
      {
        t->mlfqs_queue_level = min(t->mlfqs_queue_level + 1, 2); // Q2가 최대
        t->ticks_in_current_slice = 0; // 슬라이스 초기화
        intr_yield_on_return(); // 강제 양보
      }
    }

    // 3-B. 대기 중인 스레드 처리 (Aging 및 승급) 
    struct list_elem *e;
    // Q1, Q2 큐에 대해서만 승급(Aging) 수행 
    for (int i = 1; i <= 2; i++)
    {
      /* * (주의) 리스트를 순회하면서 원소를 제거/이동할 수 있으므로
       * e를 루프 내에서 수동으로 증가시키는 안전한 순회 방식을 사용합니다.
       */
      for (e = list_begin(&mlfqs_ready_q[i]); e != list_end(&mlfqs_ready_q[i]); /* e는 루프 내에서 증가*/ )
      {
        struct thread *th = list_entry(e, struct thread, elem);
        th->mlfqs_age++;

        if (th->mlfqs_age >= AGE_LIMIT)
        {
          // 다음 원소를 미리 저장 (현재 원소는 리스트에서 제거됨) 
          e = list_next(e); 
          
          list_remove(&th->elem); // 현재 큐에서 제거
          
          th->mlfqs_queue_level--; // 승급
          th->mlfqs_age = 0;       // age 초기화
          
          // 상위 큐(Q0 또는 Q1)의 맨 뒤에 추가 
          list_push_back(&mlfqs_ready_q[th->mlfqs_queue_level], &th->elem);
        }
        else
        {
          // age가 다 차지 않았으면 다음 스레드로 이동 
          e = list_next(e);
        }
      }
    }

    // 3-C. MLFQS 선점 확인 
    // 현재 스레드가 idle이 아닐 때만 확인 
    if (t != idle_thread)
    {
      // 현재 Q1 실행 중인데 Q0에 스레드가 있거나, 
      if (t->mlfqs_queue_level == 1 && !list_empty(&mlfqs_ready_q[0]))
        intr_yield_on_return();
      // 현재 Q2 실행 중인데 Q0 또는 Q1에 스레드가 있으면 선점 
      else if (t->mlfqs_queue_level == 2 && (!list_empty(&mlfqs_ready_q[0]) || !list_empty(&mlfqs_ready_q[1])))
        intr_yield_on_return();
    }
  }
  else
  {
    // ========= 선점형 우선순위 스케줄러 로직 (Aging) ========= 

    struct list_elem *e;

    // 3-D. 대기 중인 스레드 처리 (Aging) 
    /* * ready_list를 순회하며 age 증가 및 우선순위 상승
     * (마찬가지로 안전한 리스트 순회 방식 사용)
     */
    for (e = list_begin(&ready_list); e != list_end(&ready_list); /* e는 루프 내에서 증가 */)
    {
      struct thread *th = list_entry(e, struct thread, elem);
      th->age++;

      /* Age가 20에 도달했고, 우선순위가 최대가 아니면 */
      if (th->age >= AGE_LIMIT && th->priority < PRI_MAX)
      {
          /* * (참고) 요구사항의 "PRI_DEFAULT까지 회복"은 PRI_MAX의
           * 오기일 가능성이 높습니다. 기아 상태 방지를 위해
           * PRI_MAX까지 올리는 것이 일반적입니다.
           */
          th->priority++; // 우선순위 1 상승
          th->age = 0;    // age 초기화

          /* * 우선순위가 변경되었으므로 리스트 정렬을 위해
           * 스레드를 제거했다가 다시 삽입합니다.
           */
          e = list_next(e); // 다음 원소 미리 저장
          list_remove(&th->elem);
          list_insert_ordered(&ready_list, &th->elem, thread_priority_compare, NULL);
      }
      else
      {
        if (th->age >= AGE_LIMIT)
          th->age = 0; // (최대 우선순위 도달 시) age만 초기화

        e = list_next(e); // 다음 스레드로 이동
      }
    }

    /* 3-E. Priority 선점 확인 */
    /* * Aging으로 인해 ready_list의 최고 우선순위 스레드가 
     * 현재 실행 중인 스레드보다 우선순위가 높아졌는지 확인
     */
    if (t != idle_thread && !list_empty(&ready_list))
    {
      struct thread *highest_ready = list_entry(list_front(&ready_list), struct thread, elem);
      if (highest_ready->priority > t->priority)
      {
        intr_yield_on_return(); // 선점
      }
    }
  }
    /* ----- 새로운 스케줄링 로직 끝 ----- */
   //-------------------------------------------------------------------------

   
   
}

/* Prints thread statistics. */
void
thread_print_stats (void)
{
    printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
            idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux)
{
    struct thread *t;
    struct kernel_thread_frame *kf;
    struct switch_entry_frame *ef;
    struct switch_threads_frame *sf;
    tid_t tid;
    enum intr_level old_level;

    ASSERT (function != NULL);

    /* Allocate thread. */
    t = palloc_get_page (PAL_ZERO);
    if (t == NULL)
        return TID_ERROR;

    /* Initialize thread. */
    init_thread (t, name, priority);
    tid = t->tid = allocate_tid ();

    /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
    old_level = intr_disable ();

    /* Stack frame for kernel_thread(). */
    kf = alloc_frame (t, sizeof *kf);
    kf->eip = NULL;
    kf->function = function;
    kf->aux = aux;

    /* Stack frame for switch_entry(). */
    ef = alloc_frame (t, sizeof *ef);
    ef->eip = (void (*) (void))kernel_thread;

    /* Stack frame for switch_threads(). */
    sf = alloc_frame (t, sizeof *sf);
    sf->eip = switch_entry;
    sf->ebp = 0;

    intr_set_level (old_level);

    /* Add to run queue. */
    thread_unblock (t);

    return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void)
{
    ASSERT (!intr_context ());
    ASSERT (intr_get_level () == INTR_OFF);

    thread_current ()->status = THREAD_BLOCKED;
    schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t)
{
    enum intr_level old_level;

    ASSERT (is_thread (t));

    old_level = intr_disable ();
    ASSERT (t->status == THREAD_BLOCKED);

   //---------------------------------------------------------------------
   //아래 원본
    //list_push_back (&ready_list, &t->elem);
   //---------------------------------------------------------------------
   

//---------------------------------------------------------------------
/* ----- 스케줄러별 큐 추가 로직 ----- */
  if (thread_mlfqs) 
  {
    t->mlfqs_age = 0; // 큐에 추가될 때마다 age 초기화
    t->ticks_in_current_slice = 0; // 타임 슬라이스도 초기화
    /* MLFQS는 큐 레벨에 맞게 큐의 '뒤'에 추가 (FIFO) */
    list_push_back(&mlfqs_ready_q[t->mlfqs_queue_level], &t->elem);
  } 
  else 
  {
    t->age = 0; // 큐에 추가될 때마다 age 초기화
    /* 우선순위 스케줄러는 '정렬된 위치'에 추가 */
    list_insert_ordered(&ready_list, &t->elem, thread_priority_compare, NULL);
  }
  /* ----- 로직 끝 ----- */
//---------------------------------------------------------------------



   t->status = THREAD_READY;



//--------------------------------------------------------------------
/* ----- 선점 로직 추가 ----- */
  /* * 새로 unblock된 스레드 't'가 현재 실행 중인 스레드보다 
   * 우선순위가 높으면 즉시 선점합니다.
   * (인터럽트 핸들러 내에서 호출된 경우 실제 yield는 핸들러 종료 시 발생)
   */
  if (thread_current() != idle_thread)
  {
    if (thread_mlfqs)
    {
      /* MLFQS: 큐 레벨이 더 높으면 (숫자가 작으면) 선점 */
      if (t->mlfqs_queue_level < thread_current()->mlfqs_queue_level)
      {
        if (intr_context())
          intr_yield_on_return();
        else
          thread_yield();
      }
    }
    else
    {
      /* Priority: 우선순위가 더 높으면 선점 */
      if (t->priority > thread_current()->priority)
      {
         if (intr_context())
          intr_yield_on_return();
         else
          thread_yield();
      }
    }
  }
  /* ----- 선점 로직 끝 ----- */
//--------------------------------------------------------------------


   
    intr_set_level (old_level);
}

static void
update_next_tick_to_wakeup (int64_t tick)
{
    next_tick_to_wakeup = 
        (next_tick_to_wakeup > tick) ? tick : next_tick_to_wakeup;
}

int64_t
get_next_tick_to_wakeup (void)
{
    return next_tick_to_wakeup;
}

/* Wakes up this thread after ticks */
void
thread_sleep (int64_t tick)
{
    struct thread *cur;
    enum intr_level old_level;

    old_level = intr_disable ();
    cur = thread_current ();

    ASSERT (cur != idle_thread);

    update_next_tick_to_wakeup (cur->wakeup_tick = tick);
    list_push_back (&sleep_list, &cur->elem);

    thread_block ();

    intr_set_level (old_level);
}

void
thread_wakeup (int64_t current_tick)
{
    struct list_elem *e;

    next_tick_to_wakeup = INT64_MAX;

    e = list_begin (&sleep_list);
    while (e != list_end (&sleep_list))
    {
        struct thread *t = list_entry (e, struct thread, elem);
        if (current_tick >= t->wakeup_tick)
        {
            e = list_remove (&t->elem);
            thread_unblock (t);
        }
        else
        {
            e = list_next (e);
            update_next_tick_to_wakeup (t->wakeup_tick);
        }
    }
}

/* Returns the name of the running thread. */
const char *
thread_name (void)
{
    return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void)
{
    struct thread *t = running_thread ();

    /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
    ASSERT (is_thread (t));
    ASSERT (t->status == THREAD_RUNNING);

    return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void)
{
    return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void)
{
    ASSERT (!intr_context ());

#ifdef USERPROG
    process_exit ();
#endif

    /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
    intr_disable ();
    list_remove (&thread_current ()->allelem);
    thread_current ()->status = THREAD_DYING;
    schedule ();
    NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void)
{
    struct thread *cur = thread_current ();
    enum intr_level old_level;

    ASSERT (!intr_context ());

    old_level = intr_disable ();
   
    if (cur != idle_thread) {
       // 이게 원본 list_push_back (&ready_list, &cur->elem);



   //-----------------------------------------------------------------------
   /* ----- 스케줄러별 큐 추가 로직 ----- */
    if (thread_mlfqs)
    {
      cur->mlfqs_age = 0; // 큐에 다시 들어갈 때 age 초기화
      list_push_back(&mlfqs_ready_q[cur->mlfqs_queue_level], &cur->elem);
    }
    else
    {
      cur->age = 0; // 큐에 다시 들어갈 때 age 초기화
      list_insert_ordered(&ready_list, &cur->elem, thread_priority_compare, NULL);
    }
    /* ----- 로직 끝 ----- */
   //----------------------------------------------------------------------

       
       
    }
    cur->status = THREAD_READY;
    schedule ();
    intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
    struct list_elem *e;

    ASSERT (intr_get_level () == INTR_OFF);

    for (e = list_begin (&all_list); e != list_end (&all_list);
         e = list_next (e))
        {
            struct thread *t = list_entry (e, struct thread, allelem);
            func (t, aux);
        }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority)
{
    // 이게 원본
   //thread_current ()->priority = new_priority;



   //-----------------------------------------------------
   /* * 우선순위를 '낮췄을' 경우, 
   * ready_list에 더 높은 우선순위 스레드가 있다면 선점당해야 합니다.
   * (MLFQS 모드에서는 이 함수가 사용되지 않거나, 다른 방식(donation)으로 처리됨)
   */
   if (!thread_mlfqs)
   {



      //----------------------------------------------
      // 2차 수정
      thread_current ()->original_priority = new_priority;

      thread_recalculate_priority(thread_current());
      //---------------------------------------------


      
      thread_check_preemption();
   }
   //-----------------------------------------------------


   
}

/* Returns the current thread's priority. */
int
thread_get_priority (void)
{
    return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED)
{
    /* Not yet implemented. */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void)
{
    /* Not yet implemented. */
    return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void)
{
    /* Not yet implemented. */
    return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void)
{
    /* Not yet implemented. */
    return 0;
}



//-------------------------------------------------------------
// 2차 수정
/*
 * 스레드 t의 유효 우선순위(t->priority)를 재계산합니다.
 * t->original_priority와 t가 보유한 모든 락을 기다리는
 * 스레드들의 우선순위 중 가장 높은 값으로 설정합니다.
 */
void
thread_recalculate_priority(struct thread *t)
{
  /* 1. 기본 우선순위는 original_priority */
  int max_priority = t->original_priority;

  /* 2. 보유한 락 리스트를 순회 */
  if (!list_empty(&t->locks_i_hold))
  {
    struct list_elem *e;
    for (e = list_begin(&t->locks_i_hold); e != list_end(&t->locks_i_hold); e = list_next(e))
    {
      struct lock *l = list_entry(e, struct lock, elem);

      /* 3. 각 락의 대기열(waiters)을 확인 */
      if (!list_empty(&l->semaphore.waiters))
      {
        /*
         * sema_down에서 이미 우선순위로 정렬했으므로
         * 맨 앞의 스레드가 가장 높은 우선순위를 가집니다.
         */
        struct thread *waiter = list_entry(list_front(&l->semaphore.waiters), 
                                           struct thread, elem);
        
        /* 4. 더 높은 우선순위가 있다면 갱신 */
        if (waiter->priority > max_priority)
        {
          max_priority = waiter->priority;
        }
      }
    }
  }

  /* 5. 최종 유효 우선순위 설정 */
  t->priority = max_priority;
}
//-------------------------------------------------------------



/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED)
{
    struct semaphore *idle_started = idle_started_;
    idle_thread = thread_current ();
    sema_up (idle_started);

    for (;;)
        {
            /* Let someone else run. */
            intr_disable ();
            thread_block ();

            /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
            asm volatile ("sti; hlt" : : : "memory");
        }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux)
{
    ASSERT (function != NULL);

    intr_enable (); /* The scheduler runs with interrupts off. */
    function (aux); /* Execute the thread function. */
    thread_exit (); /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void)
{
    uint32_t *esp;

    /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
    asm ("mov %%esp, %0" : "=g"(esp));
    return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
    return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
    ASSERT (t != NULL);
    ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT (name != NULL);


   
    memset (t, 0, sizeof *t);
   
    t->status = THREAD_BLOCKED;
    strlcpy (t->name, name, sizeof t->name);
    t->stack = (uint8_t *)t + PGSIZE;
    t->priority = priority;

   //-----------------------------------------------------------------
    /* ----- 스케줄링 변수 초기화 추가 ----- */
    t->age = 0;
    t->mlfqs_queue_level = 0;     /* 모든 스레드는 Q0에서 시작 */
    t->mlfqs_age = 0;
    t->ticks_in_current_slice = 0;
    /* -----  추가 끝 ----- */
   //-------------------------------------------------------------------


   //---------------------------------------------------------------------------------
   /* ----- 추가된 초기화 ----- */
    t->original_priority = priority;       /* 원래 우선순위 설정 */
    list_init(&t->locks_i_hold);           /* 보유 락 리스트 초기화 */
    t->lock_im_waiting_for = NULL;         /* 대기 락 없음으로 초기화 */
    /* ----- 추가 끝 ----- */
   //----------------------------------------------------------------------------------


   
    t->magic = THREAD_MAGIC;
    list_push_back (&all_list, &t->allelem);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size)
{
    /* Stack data is always allocated in word-size units. */
    ASSERT (is_thread (t));
    ASSERT (size % sizeof (uint32_t) == 0);

    t->stack -= size;
    return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void)
{
   /* 이게 원본
    if (list_empty (&ready_list))
        return idle_thread;
    else
        return list_entry (list_pop_front (&ready_list), struct thread, elem);
        */



//---------------------------------------------------------
   if (thread_mlfqs)
   {
    /* MLFQS: Q0 -> Q1 -> Q2 순서로 확인 */
    if (!list_empty(&mlfqs_ready_q[0]))
      return list_entry(list_pop_front(&mlfqs_ready_q[0]), struct thread, elem);
    if (!list_empty(&mlfqs_ready_q[1]))
      return list_entry(list_pop_front(&mlfqs_ready_q[1]), struct thread, elem);
    if (!list_empty(&mlfqs_ready_q[2]))
      return list_entry(list_pop_front(&mlfqs_ready_q[2]), struct thread, elem);
   }
   else
   {
    /* Priority: 정렬된 ready_list의 맨 앞(최고 우선순위)을 꺼냄 */
    if (!list_empty(&ready_list))
      return list_entry(list_pop_front(&ready_list), struct thread, elem);
   }
  
   return idle_thread;
//----------------------------------------------------------

   
   
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
    struct thread *cur = running_thread ();

    ASSERT (intr_get_level () == INTR_OFF);

    /* Mark us as running. */
    cur->status = THREAD_RUNNING;

    /* Start new time slice. */
    thread_ticks = 0;

#ifdef USERPROG
    /* Activate the new address space. */
    process_activate ();
#endif

    /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
    if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
        {
            ASSERT (prev != cur);
            palloc_free_page (prev);
        }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void)
{
    struct thread *cur = running_thread ();
    struct thread *next = next_thread_to_run ();
    struct thread *prev = NULL;

    ASSERT (intr_get_level () == INTR_OFF);
    ASSERT (cur->status != THREAD_RUNNING);
    ASSERT (is_thread (next));

    if (cur != next)
        prev = switch_threads (cur, next);
    thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void)
{
    static tid_t next_tid = 1;
    tid_t tid;

    lock_acquire (&tid_lock);
    tid = next_tid++;
    lock_release (&tid_lock);

    return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
