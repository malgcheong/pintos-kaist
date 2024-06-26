#include "userprog/syscall.h"

#include <stdio.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "intrinsic.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/thread.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include <threads/palloc.h>


void syscall_entry(void);
void syscall_handler(struct intr_frame *);
struct page* check_address(void *addr);
void check_valid_buffer(void *buffer, size_t size, bool writable);
struct file *fd_to_fileptr(int fd);
void halt();
void exit(int status);
bool create(const char *name, unsigned initial_size);
bool remove(const char *name);
int open(const char *name);
int write(int fd, const void *buffer, unsigned size);
int add_file_to_fdt(struct file *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
void seek (int fd, unsigned position);
pid_t fork (const char *thread_name);
unsigned tell(int fd);
void close (int fd);
int wait (pid_t pid);
int exec(const char *cmd_line);

/* 시스템 호출.
 *
 * 이전에 시스템 호출 서비스는 인터럽트 핸들러에서 처리되었습니다
 * (예: 리눅스에서 int 0x80). 그러나 x86-64에서는 제조사가
 * 효율적인 시스템 호출 요청 경로를 제공합니다. 바로 `syscall` 명령입니다.
 *
 * syscall 명령은 Model Specific Register (MSR)에서 값을 읽어와서 동작합니다.
 * 자세한 내용은 메뉴얼을 참조하세요. */
/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081 /* 세그먼트 선택자 MSR */       /* Segment selector msr */
#define MSR_LSTAR 0xc0000082 /* Long mode SYSCALL target */ /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* eflags용 마스크 */   /* Mask for the eflags */

// check fd index
#define IS_UNVALID_FD(fd_index) (fd < fd_index || fd >= FDT_COUNT_LIMIT || t->fdt[fd] == NULL)


void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* 인터럽트 서비스 루틴은 syscall_entry가 유저랜드 스택을 커널
	 * 모드 스택으로 전환할 때까지 어떤 인터럽트도 처리해서는 안 됩니다.
	 * 따라서 FLAG_FL을 마스킹했습니다. */
	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

    lock_init(&filesys_lock);
}

/* 주요 시스템 호출 인터페이스 */
/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED) {

    int sys_num = f->R.rax;

    switch (sys_num) {
        case SYS_HALT:
            halt();
            break;
        case SYS_WRITE:
            f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
            break;
        case SYS_EXIT:
            exit(f->R.rdi);
            break;
        case SYS_FORK:
            f->R.rax = fork(f->R.rdi);
            break;
        case SYS_EXEC:
            f->R.rax = exec(f->R.rdi);
            break;
        case SYS_WAIT:
            f->R.rax = wait(f->R.rdi);
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
            f->R.rax = read(f->R.rdi,f->R.rsi,f->R.rdx);
            break;
        case SYS_SEEK:
            seek(f->R.rdi,f->R.rsi);
            break;
        case SYS_TELL:
            f->R.rax = tell(f->R.rdi);
            break;
        case SYS_CLOSE:
            close(f->R.rdi);
            break;
        case SYS_MMAP:
            f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
            break;
        case SYS_MUNMAP:
            munmap(f->R.rdi);
            break;
        default:
            thread_exit();
            break;
    }
}

/* 주소 유효성 검수하는 함수 */
struct page* check_address(void *addr) {
    struct thread *t = thread_current();

    if (addr == NULL || !is_user_vaddr(addr))  // 사용자 영역 주소인지 확인
        exit(-1);
    if (pml4_get_page(t->pml4, addr) == NULL) { // 페이지로 할당된 영역인지 확인
        if(!vm_claim_page(addr)){   // 해당 주소가 페이지에 존재하면 kva 매핑
            exit(-1);
        }
    } 
    return spt_find_page(&t->spt, addr);
}

/** Project 3: Memory Mapped Files - 버퍼 유효성 검사 */
void check_valid_buffer(void *buffer, size_t size, bool validation) {
    for (size_t i = 0; i < size; i++) {
        /* buffer가 spt에 존재하는지 검사 */
        struct page *page = check_address(buffer + i);

        if (!page || (validation && !(page->writable)))
            exit(-1);
    }
}

/* fd로 file 주소를 반환하는 함수 */
struct file *fd_to_fileptr(int fd) {
  struct thread *t = thread_current();

  // fd 값 검증
  if (fd < 0 || fd >= FDT_COUNT_LIMIT) {
    exit(-1); // 유효하지 않은 파일 디스크립터
  }
  
  struct file *file = t->fdt[fd];

  return file;
}

void halt() {
    power_off();
}

/* 현재 실행중인 스레드를 종료하는 함수 */
void exit(int status) {
    // 추후 종료 시 프로세스이름과 상태를 출력하는 메시지 추가
    struct thread *t = thread_current();
    t->exit_status = status;
    
    char *original_str = t->name; // 가정: t->name이 "dadsa-dasd o q"
    char *first_token,*save_ptr;

    first_token = strtok_r(original_str, " ",&save_ptr); // 공백을 구분자로 사용하여 첫 번째 토큰 추출

    if (first_token != NULL) {
        // 토큰이 성공적으로 추출되었다면, 이를 다루는 로직
          printf("%s: exit(%d)\n", first_token, t->exit_status);
    }
 
    thread_exit();
}

bool create(const char *name, unsigned initial_size) {
    check_address(name);

    lock_acquire(&filesys_lock);
    bool success = filesys_create(name, initial_size);
    lock_release(&filesys_lock);

    return success;
}

bool remove(const char *name) {
    check_address(name);

    lock_acquire(&filesys_lock);
    bool success = filesys_remove(name);
    lock_release(&filesys_lock);

    return success;
}

/** #Project 2: System Call - Open File */
int open(const char *file) {
    check_address(file);

    lock_acquire(&filesys_lock);
    struct file *newfile = filesys_open(file);

    if (newfile == NULL)
        goto err;

    int fd = add_file_to_fdt(newfile);

    if (fd == -1)
        file_close(newfile);

    lock_release(&filesys_lock);
    return fd;
err:
    lock_release(&filesys_lock);
    return -1;
}

/* console 출력하는 함수 */
int write(int fd, const void *buffer, unsigned size) {
    // 해당 버퍼가 code segment일지라도 buffer의 값이 변경되는 게 아님.
    // 해당 버퍼값으로 파일에 입력하는 것일뿐이므로 validation 필요 없음.
    check_valid_buffer(buffer, size, false);
    struct file *file = fd_to_fileptr(fd);
    int result;

    if (fd == STDIN_FILENO || fd == STDERR_FILENO) {
        return -1;
    }
    else if (fd == STDOUT_FILENO) {
        putbuf(buffer, size);
        result = size;
    }
    else { 
        lock_acquire(&filesys_lock);
        result = file_write(file,buffer,size);
        lock_release(&filesys_lock); 
    }

    return result;
}

// ### fdt functions -dsa

// 파일을 현재스레드의 fdt에 추가
int add_file_to_fdt(struct file *file) {
    struct thread *t = thread_current();
    struct file **fdt = t->fdt;
    int fd = t->fd_idx;

    while (t->fdt[fd] != NULL && fd < FDT_COUNT_LIMIT) {
        fd++;
    }
    if (fd >= FDT_COUNT_LIMIT) {
        return -1;
    }

    t->fd_idx = fd;
    fdt[fd] = file;
    
    // filesize(fd);
    return fd;
}

void delete_file_from_fdt(int fd) {
    //fdt 에서 해당 fd값의 엔트리 null로 초기화
    struct thread *t = thread_current();
    t->fdt[fd] = NULL;
}

// fd (첫 번째 인자)로서 열려 있는 파일의 크기가 몇바이트인지 반환하는 함수
int filesize(int fd) {
    struct file *file = fd_to_fileptr(fd);

    // 유효하지 않은 fd
    if (file == NULL) {
      return - 1;
    }
    
    int size = file_length(file); 

    return size; 
}

// buffer 안에 fd로 열린 파일로 size 바이트를 읽음
int read(int fd, void *buffer, unsigned size) {
    struct file *file = fd_to_fileptr(fd);

    // 버퍼가 유효한 주소인지 체크
    // 해당 버퍼가 code segment일 경우, 해당 버퍼에 쓰이면 안됨.
    // 검증이 필요하므로 validation을 true로 함
    check_valid_buffer(buffer, size, true);

    // fd가 0이면 (stdin) input_getc()를 사용해서 키보드 입력을 읽고 버퍼에 저장(?)
    if (fd == 0) {
        input_getc();
    }

    // 파일을 읽을 수 없는 케이스의 경우 -1 반환 , (fd값이 1인 경우 stout)
    if (file == NULL || fd == 1) {
        exit(-1); // 유효하지 않은 파일 디스크립터
    }

    // 구현 필요
    // lock을 이용해서 커널이 파일을 읽는 동안 다른 스레드가 이 파일을 읽는 것을 막아야함

    // filesys_lock 선언(syscall.h에 만들기)
    // syscall_init에도 lock 초기화함수 lock_init을 선언  
    lock_acquire(&filesys_lock);
    // 그 외는 파일 객체 찾고, size 바이트 크기 만큼 파일을 읽어서 버퍼에 넣어준다.
    off_t read_count = file_read (file, buffer, size);
    lock_release(&filesys_lock);

    return read_count;
}

// fd에서 읽거나 쓸 다음 바이트의 position을 변경해주는 함수
void seek (int fd, unsigned position) {
    struct file *file = fd_to_fileptr(fd);

    // 파일 디스크립터의 유효성을 검증
    if (file == NULL) {
        return -1;  // 유효하지 않은 파일 디스크립터로 인한 종료
    }

    file_seek (file, position);
}

unsigned tell (int fd) {
    struct file *file = fd_to_fileptr(fd);

    if (file == NULL) {
      return -1;
    }
    return file_tell(file);
}

void close (int fd) {
    struct file *file = fd_to_fileptr(fd);

    if (file == NULL) {
      return -1;
    }

    file_close(file);
    delete_file_from_fdt(fd);
}

pid_t fork (const char *thread_name) {
    check_address(thread_name);
    struct intr_frame *if_ = pg_round_up(&thread_name) - sizeof(struct intr_frame);
    return process_fork(thread_name, if_);
}

int wait (pid_t pid)
{
    /* 자식 프로세스가 종료 될 때까지 대기 */
    // 커널이 부모에게 자식의 종료 상태를 반환해줘야함
    // 자식의 종료 상태(exit status)를 가져옴
    // 만약 pid (자식 프로세스)가 아직 살아있으면, 종료 될 때 까지 기다립니다.
    //  종료가 되면 그 프로세스가 exit 함수로 전달해준 상태(exit status)를 반환합니다. 
	return process_wait(pid);
}

int exec (const char *cmd_line) {
    check_address(cmd_line);
    char *fn_copy;

    off_t size = strlen(cmd_line) + 1;
    fn_copy = palloc_get_page(PAL_ZERO);
    if (fn_copy == NULL)
        return -1;
    strlcpy(fn_copy, cmd_line, size);  
    
    int result = process_exec(fn_copy);
    palloc_free_page(fn_copy);
    if (result == -1) {
        exit(-1);
    }
    return result;
}

/** Project 3: Memory Mapped Files - Memory Mapping */
void *mmap(void *addr, size_t length, int writable, int fd, off_t offset) {
    if (!addr || pg_round_down(addr) != addr || is_kernel_vaddr(addr) || is_kernel_vaddr(addr + length))
        return NULL;

    if (offset != pg_round_down(offset) || offset % PGSIZE != 0)
        return NULL;

    if (spt_find_page(&thread_current()->spt, addr))
        return NULL;

    struct file *file = fd_to_fileptr(fd);

    if ((file >= STDIN_FILENO && file <= STDERR_FILENO) || file == NULL)
        return NULL;

    if (file_length(file) == 0 || (long)length <= 0)
        return NULL;

    return do_mmap(addr, length, writable, file, offset);
}

/** Project 3: Memory Mapped Files - Memory Unmapping */
void munmap(void *addr) {
    do_munmap(addr);
}
