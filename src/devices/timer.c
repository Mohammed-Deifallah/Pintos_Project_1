#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/floated.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif


/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void real_time_delay (int64_t num, int32_t denom);


// added with us
struct list list;
void foreach (void);


/* Sets up the timer to interrupt TIMER_FREQ times per second,
   and registers the corresponding interrupt. */
void
timer_init (void)
{
  pit_configure_channel (0, 2, TIMER_FREQ);
  intr_register_ext (0x20, timer_interrupt, "8254 Timer");
  list_init(&list);
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void)
{
  unsigned high_bit, test_bit;

  ASSERT (intr_get_level () == INTR_ON);
  printf ("Calibrating timer...  \n");

  /* Approximate loops_per_tick as the largest power-of-two
     still less than one timer tick. */
  loops_per_tick = 1u << 10;
  while (!too_many_loops (loops_per_tick << 1))
    {
      loops_per_tick <<= 1;
      ASSERT (loops_per_tick != 0);
    }

  /* Refine the next 8 bits of loops_per_tick. */
  high_bit = loops_per_tick;
  for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
    if (!too_many_loops (high_bit | test_bit))
      loops_per_tick |= test_bit;

  printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void)
{
  enum intr_level old_level = intr_disable ();
  int64_t t = ticks;
  intr_set_level (old_level);
  return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then)
{
  return timer_ticks () - then;
}

/* greater comparator to compare two threads to decide which thread
   will insert before the other. */

bool greater_compare_by_priority(struct list_elem *e1 , struct list_elem *e2 , void *aux){
  struct thread* t1 = list_entry(e1, struct thread ,elem);
  struct thread* t2 = list_entry(e2 ,struct thread ,elem);
  return t1->priority > t2->priority;
}


/* Sleeps for approximately TICKS timer ticks.  Interrupts must
   be turned on. */
void
timer_sleep (int64_t ticks)
{
  if(ticks <= 0) return;
  ASSERT (intr_get_level () == INTR_ON);

  enum intr_level old_level;
  old_level = intr_disable ();

  //the time to be finished in.
  thread_current()->time = ticks + timer_ticks();
  list_insert_ordered (&list, &thread_current()->elem, greater_compare_by_priority, NULL);
  thread_block();
  intr_set_level (old_level);
}

/* Sleeps for approximately MS milliseconds.  Interrupts must be
   turned on. */
void
timer_msleep (int64_t ms)
{
  real_time_sleep (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts must be
   turned on. */
void
timer_usleep (int64_t us)
{
  real_time_sleep (us, 1000 * 1000);
}

/* Sleeps for approximately NS nanoseconds.  Interrupts must be
   turned on. */
void
timer_nsleep (int64_t ns)
{
  real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Busy-waits for approximately MS milliseconds.  Interrupts need
   not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_msleep()
   instead if interrupts are enabled. */
void
timer_mdelay (int64_t ms)
{
  real_time_delay (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts need not
   be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_usleep()
   instead if interrupts are enabled. */
void
timer_udelay (int64_t us)
{
  real_time_delay (us, 1000 * 1000);
}

/* Sleeps execution for approximately NS nanoseconds.  Interrupts
   need not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_nsleep()
   instead if interrupts are enabled.*/
void
timer_ndelay (int64_t ns)
{
  real_time_delay (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void)
{
  printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}


/* Timer interrupt handler. */
void foreach (void)
{
  if(list_empty(&list)){
    return;
  }
  struct thread *t;
  struct list_elem *e = list_begin(&list);
  while(e != list_end(&list)){
    t = list_entry (e, struct thread, elem);
    if(t->time <= timer_ticks()){
      t->time = 0;
      e = list_remove(e);
      thread_unblock(t);
    } else{
      e = list_next(e);
    }
  }
  //thread_yield();
}

static void
timer_interrupt (struct intr_frame *args UNUSED)
{

  ticks++;

  if(thread_mlfqs){
    //foreach();

    //start BSD

    /* updating the recent_cpu value for all threads e`very second not every tick and then recalculate their priority value*/
    if(thread_current() != idle_thread){
      thread_current()->recent_cpu = ADD_INT(thread_current()->recent_cpu, 1);
    }

    /* updating the load average value every second. */

    if(ticks % TIMER_FREQ == 0){
      int ready_running_threads = list_empty(&ready_list) ? 0 : list_size(&ready_list);
      if(thread_current() != idle_thread) ready_running_threads++;

      /* load_avg = (59/60)*load_avg + (1/60)*ready_threads, */
      load_avg = ADD(DIV_INT(MUL_INT(load_avg, 59), 60) ,
                    DIV_INT(CONVERT_TO_FP(ready_running_threads), 60));
    }


    /* updating the recent_cpu value for all threads every second not every tick and then recalculate their priority value*/
    if(timer_ticks () % TIMER_FREQ == 0){
      struct list_elem *e;
      struct thread *t;
      for(e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)){
          t = list_entry(e, struct thread, allelem);
          if(t == idle_thread) continue;

          /* recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice. */
          t->recent_cpu = ADD_INT(MUL(DIV(MUL_INT(load_avg, 2),
                            ADD_INT(MUL_INT(load_avg, 2), 1)) , t->recent_cpu) , t->nice);
      }
    }

    if(timer_ticks() % 4 == 0){
      struct list_elem *e;
      int ind = 0;
      for(e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)){
          struct thread *t = list_entry(e, struct thread, allelem);
          if(t == idle_thread) continue;

          /*  priority = PRI_MAX - (recent_cpu / 4) - (nice * 2). */
          t->priority = PRI_MAX - CONVERT_TO_INT_TOWARD_ZERO(DIV_INT(t->recent_cpu, 4)) - (t->nice * 2);
          t->priority = (t->priority > PRI_MAX) ? PRI_MAX : (t->priority < PRI_MIN ? PRI_MIN : t->priority) ;

          if(e == NULL)break;
      }
      if(!list_empty(&ready_list))
        list_sort(&ready_list, greater_compare_by_priority, NULL);
    }
  }

  thread_tick();
  foreach();
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops)
{
  /* Wait for a timer tick. */
  int64_t start = ticks;
  while (ticks == start)
    barrier ();


  /* Run LOOPS loops. */
  start = ticks;
  busy_wait (loops);

  /* If the tick count changed, we iterated too long. */
  barrier ();
  return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops)
{
  while (loops-- > 0)
    barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom)
{
  /* Convert NUM/DENOM seconds into timer ticks, rounding down.

        (NUM / DENOM) s
     ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
     1 s / TIMER_FREQ ticks
  */
  int64_t ticks = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks > 0)
    {
      /* We're waiting for at least one full timer tick.  Use
         timer_sleep() because it will yield the CPU to other
         processes. */
      timer_sleep (ticks);
    }
  else
    {
      /* Otherwise, use a busy-wait loop for more accurate
         sub-tick timing. */
      real_time_delay (num, denom);
    }
}

/* Busy-wait for approximately NUM/DENOM seconds. */
static void
real_time_delay (int64_t num, int32_t denom)
{
  /* Scale the numerator and denominator down by 1000 to avoid
     the possibility of overflow. */
  ASSERT (denom % 1000 == 0);
  busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
}
