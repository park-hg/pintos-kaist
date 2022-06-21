#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

static void argument_stack(char **argv, int argc, struct intr_frame *if_);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	// page 가상메모리의 단위
	fn_copy = palloc_get_page (0);	// 하나의 가용 페이지를 할당하고 그 커널 가상 주소를 리턴.
	if (fn_copy == NULL)
		return TID_ERROR;
	// fn_copy 주소 공간에 file_name을 복사해 넣어주고, 4kb로 길이 한정한다(임의로 준 크기)
	strlcpy (fn_copy, file_name, PGSIZE);

	char *save_ptr;
	// printf("file_name : %s\n", file_name);	// file_name : args-single onearg
	strtok_r(file_name, " ", &save_ptr);	/* cut args to set only filename to thread name */
	// printf("file_name : %s\n", file_name);  // file_name : args-single
	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	// process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created.
	 현재 프로세스를 "name"으로 복제합니다.
	 새 프로세스의 스레드 ID를 반환하거나 스레드를 생성할 수 없는 경우 TID_ERROR를 반환합니다.
 */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	/* ------------- project 2 ------------------ */
	struct thread *parent = thread_current();

	// memcpy(&parent->parent_if, if_, sizeof(struct intr_frame));

	tid_t tid = thread_create(name, parent->priority, __do_fork, parent);

	if (tid == TID_ERROR) {
		return TID_ERROR;
	}

	struct thread *child = get_child_by_tid(tid);

	sema_down(&child->fork_sema);

	if (child->exit_status == -1) {
		return TID_ERROR;
	}

	return tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va)){
		return true; // return false ends pml4_for_each, which is undesirable - just return true to pass this kernel va
	}

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (parent_page == NULL) {
		return false;
	}
	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);
	if (newpage == NULL) {
		printf("[fork-duplicate] failed to palloc new page\n"); // #ifdef DEBUG
		return false;
	}
	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte); // *PTE is an address that points to parent_page

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		printf("Failed to map user virtual page to given physical frame\n"); // #ifdef DEBUG
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. 
 * 
 * 부모의 실행 컨텍스트를 복사하는 스레드 함수입니다.
 * 힌트) parent->tf는 프로세스의 사용자 및 컨텍스트를 보유하지 않습니다.
 * 즉, process_fork의 두 번째 인수를 이 함수에 전달해야 합니다.
 * 
 * */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *child = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	/* -------- Project 2 ----------- */
	struct intr_frame *parent_if = &parent->parent_if;
	bool succ = true;
	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0; /* child's return value = 0 */ // 부모자식 구분
	/* 2. Duplicate PT */
	child->pml4 = pml4_create();
	if (child->pml4 == NULL)
		goto error;

	process_activate (child);
#ifdef VM
	supplemental_page_table_init (&child->spt);
	if (!supplemental_page_table_copy (&child->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	if (parent->fd_idx == FDCOUNT_LIMIT){
		goto error;
	}

	for (int i = 0; i < FDCOUNT_LIMIT; i++){
		struct file *file = parent->fd_table[i];
		if(file == NULL)
			continue;
		// If 'file' is already duplicated in child, don't duplicate again but share it

		struct file *new_file;
		if (file > 2)
			new_file = file_duplicate(file);
		else
			new_file = file;
		
		child->fd_table[i] = new_file;
	}
	child->fd_idx = parent->fd_idx;

	// child loaded successfully, wake up parent in process_fork
	sema_up(&child->fork_sema);

	// process_init (); 안함?

	/* Finally, switch to the newly created process. */
	if (succ)
	{
		do_iret (&if_);
		// printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ %d", thread_current()->tid);
	}
		
error:
	child->exit_status = TID_ERROR;
	sema_up(&child->fork_sema);
	// thread_exit ();
	exit(TID_ERROR);
	/* ------------------------------- */
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.  
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. 
	 * 
	 * 우리는 스레드 구조에서 intr_frame을 사용할 수 없습니다.
	 * 현재 스레드가 다시 예약되면 실행 정보가 멤버에 저장되기 때문입니다
	 * */

	// 인터럽트 당한, 원래 실행 중이었던 프로세스의 정보일부가 담긴다. (쓰레드끼리 공유하는부분 - 추측)
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;  
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();  // 현재 프로세스가 사용하고 있던 pml4를 모두 반환한다.  

	/* And then load the binary */
	// printf("load_file_name: %s\n", file_name);  // => load_file_name: args-single onearg
	success = load (file_name, &_if);

	/* If load failed, quit. */  
	palloc_free_page (file_name);
	if (!success)
		return -1;

	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);

	// 지금까지 바꿔준 인터럽트 프레임의 값으로 레지스터 값을 바꿔준다. 
	// 즉, 새 프로세스로 switching한다.
	// do_iret()은 인터럽트 프레임을 레지스터에 넣어 실행시키고 나면 다시 그 이전 프로세스로 되돌아오지 않는다.
	// 여기서 do_iret()에 exec() 내에서 만들어준 _if 구조체 내 값으로 레지스터 값을 수정한다. 
	// 여기서 SEL_UDSEG, SEL_UCSEG는 각각 유저 메모리의 데이터, 코드 선택자로 유저 메모리에 있는 데이터, 코드 세그먼트를 가리키는 주소값이다.
	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	// for (int i = 0; i < 100000000; i ++) {} // infinite loop 추가
	// while(1){};

	// 현재쓰레드
	struct thread *curr = thread_current();
	// 죽일자식 쓰레드
	struct thread *child = get_child_by_tid(child_tid);

	if (child == NULL) {
		return -1;
	}

	// 자식 프로세스 안에 있는 wait_sema에서 wait함으로써
	// 자식 프로세스가 종료될때까지 BLOCK 상태로 sema wait list에서 기다림
	sema_down(&child->wait_sema);

	// 자식 프로세스의 process_exit이 불려 종료되는 과정에서 자식이 자신의 wait_sema를 UP해준다.
	// 그럼 부모는 자식의 exit_status를 가져온다. (자식은 부모가 리스트에서 삭제할때까지 대기)
	int exit_status = child->exit_status; // 0

	// 부모 프로세스의 child list에서 자식 프로세스를 없앤다.
	list_remove(&child->child_elem);	// 부모 프로세스의 child list에서 자식 프로세스를 없앤다.
	
	// 자식이 SLEEP하고 있던 exit_sema를 fg UP해줌으로써
	// 자식이 종료 과정을 계속할 수 있도록 한다.(thread_exit)
	sema_up(&child->exit_sema);
	return exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	// fork일떄는 자식
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	
	/* 파일 디스크립터 테이블의 최대값을 이용해 파일 디스크립터
		의 최소값인 2가 될 때까지 파일을 닫음 */
	for (int i = 0; i < FDCOUNT_LIMIT; i++) {
		close(i);
	}

#ifdef VM
   	struct hash_iterator iter;

	if (curr->spt.hash_table.buckets != NULL) {
		hash_first (&iter, &curr->spt.hash_table);
		while (hash_next (&iter))
		{
			struct page *p = hash_entry (hash_cur (&iter), struct page, h_elem);

			if (p != NULL && p->is_mmapped)
				munmap(p->va);
		}
	}
#endif

	// 프로세스 종료가 일어날 경우 프로세스에 열려있는 모든 파일을 닫음
	palloc_free_multiple(curr->fd_table, FDT_PAGES);
	file_close(curr->running);

	// why clean up? do_iret()을 사용하여 PC와 레지스터의 값을 바꿔주어 실행시킬 프로세스로 전환된다.
	// 그리고 다시 Caller로 돌아오는 일이 없다.
	process_cleanup (); // 안하면 multi-oom Fail뜬다.  

	// 자식프로세스 종료 후 (대기하던)부모프로세스가 다음과정 할 수 있도록한다. 
	sema_up(&curr->wait_sema);

	// 자식프로세스는 부모가 리스트에서 제외할동안(종료) 대기
	sema_down(&curr->exit_sema);

}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). 
		 * 
		 * 여기서 올바른 순서가 중요합니다.
		 * 페이지 디렉토리를 전환하기 전에 cur->pageir를 NULL로 설정해야 타이머 인터럽트가 프로세스 페이지 디렉토리로 다시 전환되지 않습니다.
		 * 프로세스의 페이지 디렉토리를 삭제하기 전에 기본 페이지 디렉토리를 활성화해야 합니다.
		 * 그렇지 않으면 활성 페이지 디렉토리가 해제되어 지워집니다.
		 * */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset; 	// 끝나는지점
	uint64_t p_vaddr;  	// 주소
	uint64_t p_paddr;  	// 주소
	uint64_t p_filesz; 	// 사이즈
	uint64_t p_memsz;  	// 사이즈
	uint64_t p_align;  	// 패딩맞추기
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;


	/* ---------project2 -----------*/
	char *argv_list[64];// 인자들 담을배열
	char *token, *save_ptr;
	int argv_cnt = 0;
	// pintos --fs-disk=10 -p tests/userprog/args-single:args-single -- -q -f run 'args-single onearg'
	// printf("file_name: %s\n", file_name);		// file_name: args-single onearg
	for (token = strtok_r (file_name, " ", &save_ptr); token != NULL; token = strtok_r (NULL, " ", &save_ptr)){
   	argv_list[argv_cnt] = token;
		// printf("token: %s\n", token);  // => token: args-single, token: onearg 두번출력
		argv_cnt++;
	}
	/* ---------------------------- */

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();	 // 페이지 테이블 만들기
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	// 현재 함수와 이름 모두 들어옴 => 이름만 들어오도록 수정해야함, filesys_open => 파일을 여는 함수
	// printf("file_name: %s\n", file_name);
	lock_acquire(&filesys_lock);
	// printf("thread id %d, load?? file name %s\n", thread_current()->tid, file_name);
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		lock_release(&filesys_lock);	
		goto done;
	}

	file_deny_write(file);
	t->running = file;
	lock_release(&filesys_lock);	
	
	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
	argument_stack(argv_list, argv_cnt, if_);	
	success = true;

done:
	/* We arrive here whether the load is successful or not. */

	return success;
}

void argument_stack(char **arg, int count, struct intr_frame *if_)
{
	int i;
	for (i = count - 1; i > -1; i--) {
		if_->rsp -= (strlen(arg[i]) + 1);
		memcpy(if_->rsp, arg[i], strlen(arg[i]) + 1);
		arg[i] = if_->rsp;
	}

	memset(if_->rsp & ~7, 0, if_->rsp - (if_->rsp & ~7));
	if_->rsp &= ~7;

	if_->rsp -= sizeof(char *);
	memset(if_->rsp, 0, sizeof(char *));

	for (i = count - 1; i > -1; i--) {
		if_->rsp -= sizeof(char *);
		memcpy(if_->rsp, &arg[i], sizeof(char *));
	}

	if_->R.rdi = count;
	if_->R.rsi = if_->rsp;

	/* return address */
	if_->rsp -= sizeof(void (*)());
	memset(if_->rsp, 0, sizeof(void (*)()));
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, struct file_info *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	if (file_read_at (aux->file, page->frame->kva, aux->read_bytes, aux->ofs) != aux->read_bytes) {
		vm_dealloc_page (page);

		return false;
	}
	memset (page->frame->kva + aux->read_bytes, 0, aux->zero_bytes);

	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		struct file_info *f_info = (struct file_info *) calloc (1, sizeof (struct file_info));

		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		f_info->file = file;
		f_info->ofs = ofs;
		f_info->read_bytes = page_read_bytes;
		f_info->zero_bytes = page_zero_bytes;

		if (!vm_alloc_page_with_initializer(VM_ANON, upage,
											writable, lazy_load_segment, f_info))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	if (success = vm_alloc_page (VM_ANON | VM_STACK, stack_bottom, true)) {
		if_->rsp = USER_STACK;
	}
	return success;
}
#endif /* VM */
