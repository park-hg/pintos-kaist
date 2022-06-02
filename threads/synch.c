#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

	 세마포어 SEMA를 값으로 초기화합니다. 
	 세마포어는 이를 조작하기 위한 두 개의 원자 연산자와 함께 음이 아닌 정수입니다.

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */

void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);
	sema->value = value;
	// 해당 sema를 리스트의 첫째 자리에 넣는다.
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. 

	 세마포어에서의 다운 또는 "P" 연산.
	 SEMA의 값이 양의 값이 될 때까지 기다렸다가 이를 원자적으로 감소시킵니다.
	 이 함수는 절전 모드일 수 있으므로 인터럽트 핸들러 내에서 호출하면 안 됩니다.
	 이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만
	 절전 모드일 경우 다음 예약된 스레드가 인터럽트를 다시 켤 수 있습니다.
	 이것은 sema_down 함수입니다.
	 */

// semaphore를 획득하는 연산 - P연산
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		// 우선순위로 해야함
		list_insert_ordered(&sema->waiters, &thread_current ()->elem, &cmp_priority, NULL);
		thread_block ();
	}
	// printf("ㅗ\n");
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.
   This function may be called from an interrupt handler. 
	 
	 세마포어에서 다운 또는 "P" 연산은 세마포어가 아직 0이 아닌 경우에만 가능합니다.
	 세마포어가 감소하면 true를 반환하고 그렇지 않으면 false를 반환합니다.
	 이 함수는 인터럽트 핸들러에서 호출될 수 있다
	 */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.
   This function may be called from an interrupt handler.

	 세마포어에서의 업 또는 "V" 연산.
	 SEMA의 값을 증가시키고 SEMA를 기다리는 스레드(있는 경우)를 웨이크업합니다.
	 이 함수는 인터럽트 핸들러에서 호출될 수 있다.
*/

// Critical section 에서 작업을 마치고 semaphore를 반환하는 연산 - V연산
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();

	/* ----------- project1 ------------ */
	if (!list_empty (&sema->waiters)){
		// (Priority Scheduling-Synchronization)
		// waiter list에 있는 쓰레드의 우선순위가 변경 되었을 경우를 고려하여 waiter list를 정렬 
		list_sort(&sema->waiters, &cmp_priority, 0);
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}
	/* --------------------------------- */

	sema->value++;

	/* ----------- project1 ------------ */
	// 세마포어 해제 후 priority preemption 기능 추가
	// 현재 수행중인 스레드와 가장 높은 우선순위의 스레드의 우선순위를 비교하여 스케줄링
	if (preempt_by_priority()){
		if (intr_context()) {
			intr_yield_on_return();
		} else {
			thread_yield();
		}
	}
	intr_set_level (old_level);
	/* --------------------------------- */
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. 
	 
	 LOCK을 초기화합니다. 잠금은 주어진 시간에 최대 단일 스레드로 고정될 수 있습니다.
	 우리의 잠금은 "재귀적"이 아니다.
	 즉, 현재 잠금을 보유하고 있는 스레드가 그 잠금을 획득하려고 시도하는 것은 오류이다.
	 잠금은 초기값이 1인 세마포어의 특성화이다. 자물쇠와 그러한 세마포어의 차이는 두 가지이다.
	 첫째, 세마포어는 1보다 큰 값을 가질 수 있지만,
	 잠금은 한 번에 하나의 스레드에 의해서만 소유될 수 있다.
	 둘째, 세마포어에는 소유자가 없는데, 이는 한 스레드가
	 세마포어를 "다운"한 다음 다른 스레드가 "업"할 수 있다는 것을 의미한다.
	 이러한 제한이 부담스럽다면 자물쇠 대신 세마포어를 사용해야 한다는 좋은 신호이다
	 */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep.
	 
	필요한 경우 사용할 수 있을 때까지 절전 모드인 LOCK을 획득합니다.
	현재 스레드에 의해 잠금이 이미 고정되어 있지 않아야 합니다.
	이 함수는 절전 모드일 수 있으므로 인터럽트 핸들러 내에서 호출하면 안 됩니다.
	인터럽트가 비활성화된 상태에서 이 함수를 호출할 수 있지만,
	절전 모드가 필요한 경우 인터럽트가 다시 켜집니다.
*/
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	/* ----------- Project 1 ------------ */
	struct thread *curr = thread_current();
	// 만약 해당 lock을 누가 사용하고 있다면
	if (lock->holder) {
		curr->wait_on_lock = lock;  // 현재 스레드의 wait_on_lock에 해당 lock을 저장한다.
		// 지금 lock을 소유하고 있는 스레드의 donations에 현재 스레드를 저장한다.
		list_push_back(&lock->holder->donation_list, &curr->donation_elem);
		donate_priority();
	}
	/* ---------------------------------- */
	
	sema_down (&lock->semaphore); // lock을 얻기 위해 기다리는중

	/* ----------- Project 1 ------------ */
	curr->wait_on_lock = NULL;		// lock을 획득했으므로 대기하고 있는 lock이 이제는 없다.
	lock->holder = curr;
	/* ---------------------------------- */
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. 
	
	LOCK을 획득하려고 시도하며 실패하면 true를 반환하고 실패하면 false를 반환합니다.
	현재 스레드에 의해 잠금이 이미 고정되어 있지 않아야 합니다.
	이 함수는 절전 모드가 아니므로 인터럽트 핸들러 내에서 호출될 수 있습니다.
*/
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	/* ----------- Project 1 ------------ */
	remove_donation_list_elem(lock);  // donations 리스트에서 해당 lock을 필요로 하는 스레드를 없애준다.
	refresh_priority();		// 현재 스레드의 priority를 업데이트한다.
	/* ---------------------------------- */

	lock->holder = NULL;	// lock의 holder를 NULL로.
	sema_up (&lock->semaphore);  // sema를 UP 시켜 해당 lock에서 기다리고 있는 스레드 하나를 깨운다.
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. 
	 
	 조건 변수 COND를 초기화합니다. 
	 조건 변수는 코드 조각 하나가 조건을 신호화하고
	 협력 코드가 신호를 수신하고 그에 따라 행동할 수 있게 한다.
*/
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. 
	 
	 Atomic은 LOCK을 해제하고 COND가 다른 코드로 신호를 보내기를 기다립니다.
	 COND가 표시되면 LOCK을 다시 획득한 후 복귀합니다.
	 이 함수를 호출하기 전에 LOCK을 눌러야 합니다.
	 이 함수에 의해 구현되는 모니터는 "호아레" 스타일이 아닌 "메사" 스타일이다.
	 따라서 일반적으로 호출자는 대기 완료 후 상태를 다시 확인하고 필요한 경우 다시 기다려야 합니다.
	 주어진 조건 변수는 단일 잠금에만 연결되지만
	 하나의 잠금은 임의의 수의 조건 변수와 연관될 수 있습니다.
	 즉, 잠금에서 조건 변수에 대한 일대다 매핑이 있습니다.
	 이 함수는 절전 모드일 수 있으므로 인터럽트 핸들러 내에서 호출하면 안 됩니다.
	 인터럽트가 비활성화된 상태에서 이 함수를 호출할 수 있지만,
	 절전 모드가 필요한 경우 인터럽트가 다시 켜집니다.
	 */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);

	sema_down (&waiter.semaphore);

	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */


// 여기서는 중간에 우선순위가 바뀌었을 수 있으니 
// waiters list 내 우선순위를 점검하고(list_sort),
// 그 다음 리스트 맨 앞에 있는 우선순위가 가장 높은 스레드에 대해 sema_up을 실행한다.
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	/* ----------- Project 1 ------------ */
	if (!list_empty (&cond->waiters)){
		list_sort(&cond->waiters, &cmp_sem_priority, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
	/* ---------------------------------- */
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

/* ------------ project 1 ------------ */
/* compare thread priority value in condition variable
  1. make semaphore_elem by elem *a and *b
  2. get semaphore's waiters by semaphore_elem
  3. get thread priority of first thread of semaphore's waiters 
  compare and return
 */
static bool cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
	// semaphore_elem으로부터 각 semaphore_elem의 쓰레드 디스크립터를 획득.
	struct semaphore_elem * sema_a = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem * sema_b = list_entry(b, struct semaphore_elem, elem);
	
	struct list_elem *sema_a_elem = list_begin(&(sema_a->semaphore.waiters));
	struct list_elem *sema_b_elem = list_begin(&(sema_b->semaphore.waiters));

	struct thread *sema_a_thread = list_entry(sema_a_elem, struct thread, elem);
	struct thread *sema_b_thread = list_entry(sema_b_elem, struct thread, elem);

	// 첫 번째 인자의 우선순위가 두 번째 인자의 우선순위보다 높으면 1을 반환 낮으면 0을반환
	return (sema_a_thread->priority > sema_b_thread->priority);
}


/* if current thread want to acqurie lock and there is lock holder,
	donate current thread priority to every single thread that lock holder is waiting for 
	( depth limit = 8 according to test case ) */
void donate_priority(void) {
	int depth;
	struct thread* curr = thread_current();
	struct thread* holder = curr->wait_on_lock->holder;

	for (depth = 0; depth < 8; depth++) {
		if (!curr->wait_on_lock) break;
		holder = curr->wait_on_lock->holder;
		holder->priority = curr->priority;
		curr = holder;
	}
}

/* when current thread release THIS LOCK(argument struct lock *lock),
 the current thread remove only threads which want THIS LOCK in the current thread's donation_list.
 In which if current thread's donation list is not empty, it means current thread holds another lock. */
void remove_donation_list_elem(struct lock *lock){
	struct thread *curr = thread_current();

	if (!list_empty(&curr->donation_list)) {
		struct list_elem *e = list_begin(&curr->donation_list);
		
		for (e; e != list_end(&curr->donation_list); e = list_next(e)){
			struct thread *t = list_entry(e, struct thread, donation_elem);
			if (lock == t->wait_on_lock){
				list_remove(&t->donation_elem);
			}
		}
	}
}

/* reset current thread priority.
	if there is donated thread to currnet thread, find the bigger priority and set to current thread priority.
	if not, set current priority to initial priority.
	현재 스레드 우선 순위를 재설정합니다.
	커런트 스레드에 기부된 스레드가 있으면 더 큰 우선 순위를 찾아 현재 스레드 우선 순위로 설정합니다.
	그렇지 않으면 현재 우선 순위를 초기 우선 순위로 설정합니다.
	*/
void refresh_priority(void){
	struct thread *curr = thread_current();
	curr->priority = curr->initial_priority;

	if (!list_empty(&curr->donation_list)){
		list_sort(&curr->donation_list, &thread_donate_priority_compare, NULL);
		
		struct list_elem *donated_e = list_front(&curr->donation_list);

		int max_donated_priority = list_entry(donated_e, struct thread, donation_elem)->priority;
		if (curr->priority<max_donated_priority) {
			curr->priority = max_donated_priority;
		}
	}
}
/* ------------------- project 1 functions end ------------------------------- */