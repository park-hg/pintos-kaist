#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
/* ---------- Project 2 ---------- */
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "kernel/stdio.h"
#include "threads/palloc.h"
/* ------------------------------- */

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* ---------- Project 2 ---------- */
void check_address(const uint64_t *uaddr);

void halt (void);			/* 구현 완료 */
void exit (int status);		/* 구현 완료 */
tid_t fork (const char *thread_name, struct intr_frame *f);
int exec (const char *cmd_line);
int wait (tid_t child_tid UNUSED); /* process_wait()으로 대체 필요 */
bool create (const char *file, unsigned initial_size); 	/* 구현 완료 */
bool remove (const char *file);							/* 구현 완료 */
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
/* ------------------------------- */

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */
/* ---------- Project 2 ---------- */
const int STDIN = 0;
const int STDOUT = 1;
/* ------------------------------- */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	/* ---------- Project 2 ---------- */
	lock_init(&filesys_lock);
	/* ------------------------------- */
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	// 시스템 호출 핸들러 syscall_handler()가 제어권을 얻을 때 시스템 호출 번호는 rax에 있고
	// 인수는 %rdi, %rsi, %rdx, %r10, %r8, %r9의 순서로 전달됩니다.
	// 1번째 인자: %rdi
	// 2번째 인자: %rsi
	// 3번째 인자: %rdx
	// 4번째 인자: %r10
	// 5번째 인자: %r8
	// 6번째 인자: %r9


	/* ---------- Project 2 ---------- */
	switch(f->R.rax) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi, f);
			break;
		case SYS_EXEC:
			if (exec(f->R.rdi) == -1)
				exit(-1);
			break;
		case SYS_WAIT:
			f->R.rax = process_wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			exit(-1);
			break;
	}
	/* ------------------------------- */
}

/* ---------- Project 2 ---------- */
// 주소 값이 유저 영역 주소 값인지 확인 => 유저 영역을 벗어난 영역일 경우 프로세스 종료(exit(-1))
void check_address (const uint64_t *user_addr) {
	struct thread *curr = thread_current();
	// is_user_vaddr =>Returns true if VADDR is a user virtual address.
	// 유저 가상 메모리의 영역은 가상 주소 0부터 KERN_BASE까지이다.
	if (user_addr = NULL || !(is_user_vaddr(user_addr))|| // 'KERN_BASE'보다 높은 값의 주소값을 가지는 경우 or 주소가 NULL인경우
	pml4_get_page(curr->pml4, user_addr) == NULL)	// 포인터가 가리키는 주소가 유저 영역 내에 있지만 페이지로 할당하지 않은 영역인 경우 => 둘중하나만 써도되나 확인하기
	{
		exit(-1);
	}
}


/* Check validity of given file descriptor in current thread fd_table */
// 프로세스의 파일 디스크립터 테이블을 검색하여 파일 객체의 주소를 리턴
static struct file *get_file_from_fd_table(int fd) {
	struct thread *curr = thread_current();
	if (fd < 0 || fd >= FDCOUNT_LIMIT) {
		return NULL;
	}
	// 파일 디스크립터에 해당하는 파일 객체를 리턴 
	return curr->fd_table[fd];	/*return fd of current thread. if fd_table[fd] == NULL, it automatically returns NULL*/
}


/* Remove give fd from current thread fd_table */
// 파일 디스크립터에 해당하는 파일을 닫고 해당 엔트리 초기화
// void remove_file_from_fdt(int fd)
// {
// 	struct thread *cur = thread_current();
// 	if (fd < 0 || fd >= FDCOUNT_LIMIT) /* Error - invalid fd */
// 		return;
// 	// File Descriptor에 해당하는 파일 객체의 파일을 제거
// 	cur->fd_table[fd] = NULL;
// }

/* Find available spot in fd_talbe, put file in  */
// 파일 객체에 대한 파일 디스크립터 생성
int add_file_to_fdt(struct file *file) {
	struct thread *curr = thread_current();
	struct file **fdt = curr->fd_table;

	while (curr->fd_idx < FDCOUNT_LIMIT && fdt[curr->fd_idx]) {
		curr->fd_idx++;
	}

	if (curr->fd_idx >= FDCOUNT_LIMIT) {
		return -1;
	}

	fdt[curr->fd_idx] = file;
	return curr->fd_idx;
}


// 1. pintos를 종료시키는 시스템 콜
void halt (void) {
	power_off();
}


// 2. 현재 프로세스를 종료시키는 시스템 콜
void exit(int status) {
	// 실행중인 스레드 구조체를 가져옴
	struct thread *curr = thread_current();
	curr->exit_status = status;
	// 프로세스 종료 메시지 출력
	// 출력 양식: "프로세스이름: exit(종료상태)"
	printf("%s: exit(%d)\n", thread_name(), status);
	// 스레드 종료
	thread_exit();
}

// 3. 현재 프로세스를 복사하는 시스템 콜
tid_t fork (const char *thread_name, struct intr_frame *f) {
	// check_address(thread_name);
	return process_fork(thread_name, f);
}


// 4. 
int exec(const char *cmd_line) {
	check_address(cmd_line);
	/* 인자로 받은 파일 이름 문자열을 복사하여 이 복사본을 인자로 process_exec() 실행*/
	char *cmd_line_cp;
	// printf("cmd_line #############################: %s\n", cmd_line);
	int size = strlen(cmd_line);
	cmd_line_cp = palloc_get_page(0);
	if (cmd_line_cp == NULL) {
		exit(-1);
	}
	strlcpy (cmd_line_cp, cmd_line, size + 1);

	if (process_exec(cmd_line_cp) == -1) {
		return -1;
	}
	/* Caller 프로세스는 do_iret() 후 돌아오지 못한다. */
	NOT_REACHED();
}


// 6. 파일을 생성하는 시스템 콜
bool create (const char *file, unsigned initial_size) {
	// 파일 이름과 크기에 해당하는 파일 생성
	// 파일 생성 성공 시 true 반환, 실패 시 false 반환
	check_address(file);
	return filesys_create(file, initial_size);
}


// 7. 파일을 삭제하는 시스템 콜
bool remove (const char *file) {
	// 파일 이름에 해당하는 파일을 제거
	// 파일 제거 성공 시 true 반환, 실패 시 false 반환
	check_address(file);
	return filesys_remove(file);
}


// 8. 파일을 열 때 사용하는 시스템 콜
int open (const char *file) {
	check_address(file);
	lock_acquire(&filesys_lock);

	// 제대로 파일 생성됐는지 체크
	if (file == NULL) {
		lock_release(&filesys_lock);
		return -1;
	}

	// 열려고 하는 파일구조체 받기
	struct file *open_file = filesys_open(file);

	// 파일이 없으면 종료
	if (open_file == NULL) {
		lock_release(&filesys_lock);
		return -1;
	}

	// 만들어진 파일을 스레드 내 fdt 테이블에 추가
	int fd = add_file_to_fdt(open_file);

	// 파일을 열수 없으면 -1반환
	if (fd == -1) {
		file_close(open_file);
	}

	lock_release(&filesys_lock);
	return fd;
}


// 9. 파일의 크기를 알려주는 시스템 콜
int filesize (int fd) {
	// 사이즈를 알고싶은 파일구조체 받기
	struct file *open_file = get_file_from_fd_table(fd);	// => 파일 디스크립터에 해당하는 파일 객체를 리턴 
	// 성공 시 파일의 크기를 반환, 실패 시 -1 반환
	if (open_file == NULL) {
		return -1;
	}
	// 파일의 크기를 알려주는 함수
	return file_length(open_file);
}


// 10. 열린 파일의 데이터를 읽는 시스템 콜
int read (int fd, void *buffer, unsigned size) {
	// 유효한 주소인지 체크
	check_address(buffer);
	/* 파일에 동시 접근이 일어날 수 있으므로 Lock 사용 */
	lock_acquire(&filesys_lock);

	int read_count;
	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	struct file *file_obj = get_file_from_fd_table(fd);

	if (file_obj == NULL) {	/* if no file in fdt, return -1 */
		lock_release(&filesys_lock);
		return -1;
	}
	/* STDIN */
	/* 파일 디스크립터가 0일 경우 키보드에 입력을 버퍼에 저장 후
		 버퍼의 저장한 크기를 리턴 (input_getc() 이용) */
	if (fd == STDIN) {
		int i;
		unsigned char *buf = buffer;
		for (i = 0; i < size; i++) {
			// 키보드의 입력 버퍼에서 글자 하나씩을 받아 반환해주는 함수
			char c = input_getc();
			// 읽기 버퍼에 한 char씩 넣는다.
			*buf++ = c;
			// 종단문자 만나면 탈출
			if (c == '\0')
				break;
		}
		read_count = i;
	}
	/* STDOUT */
	else if (fd == STDOUT) {
		read_count = -1;
	}
	else {	
		/* 파일 디스크립터가 0이 아닐 경우 파일의 데이터를 크기만큼 저
			 장 후 읽은 바이트 수를 리턴*/
		read_count = file_read(file_obj, buffer, size);
	}
	lock_release(&filesys_lock);
	// 읽은 바이트 수를 리턴
	return read_count;
}


// 11. 열린 파일의 데이터를 기록하는 시스템 콜
int write (int fd, const void *buffer, unsigned size) {
	check_address(buffer);
	/* 파일에 동시 접근이 일어날 수 있으므로 Lock 사용 */
	lock_acquire(&filesys_lock);

	int write_count;
	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	struct file *file_obj = get_file_from_fd_table(fd);
	
	if (file_obj == NULL) {
		lock_release(&filesys_lock);
		return -1;
	}

	/* STDOUT */
	/* 파일 디스크립터가 1일 경우 버퍼에 저장된 값을 화면에 출력
		 후 버퍼의 크기 리턴 (putbuf() 이용) */
	if (fd == STDOUT) {
		putbuf(buffer, size);
		write_count = size;
	}
	/* STDOUT */
	else if (fd == STDIN) {
		write_count = -1;
	}
	/* 파일 디스크립터가 1이 아닐 경우 버퍼에 저장된 데이터를 크기
		 만큼 파일에 기록후 기록한 바이트 수를 리턴 */
	else {
		write_count = file_write(file_obj, buffer, size);
	}

	lock_release(&filesys_lock);
	// 기록한 바이트 수를 리턴
	return write_count;
}


// 12. 열린 파일의 위치(offset)를 이동하는 시스템 콜
void seek (int fd, unsigned position) {
	// 파일 디스크립터를 이용하여 파일 객체 검색
	struct file *file_obj = get_file_from_fd_table(fd);
	// file이 fdt에 없거나 해당 파일이 표준 입출력 파일인 경우.
	if (file_obj == NULL) {
		return;
	}
	if (fd <= 1) {
		return;
	}
	// 해당 열린 파일의 위치(offset)를 position만큼 이동	
	file_seek(file_obj, position);
}


// 13. 열린 파일의 위치(offset)를 알려주는 시스템 콜
unsigned tell (int fd) {
	// 파일 디스크립터를 이용하여 파일 객체 검색
	struct file *file_obj = get_file_from_fd_table(fd);
	if (file_obj == NULL) {
		return;
	}
	if (fd <= 1) {
		return;
	}
	// 열린 파일의 위치를 반환	
	file_tell(file_obj);	
}


// 14. 열린 파일을 닫는 시스템 콜
void close (int fd) {
	struct file *file_obj = get_file_from_fd_table(fd);

	if (file_obj == NULL) {
		return;
	}

	if (fd <= 1) {
		return;
	}

	thread_current()->fd_table[fd] = NULL;
	// remove_file_from_fdt(fd);
	// file_close(file_obj);
}

/* ------------------------------- */