#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

//system call
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <list.h>
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);


//sysrtem call
void check_addr(const uint64_t *uaddr);		

void halt (void);
void exit (int status);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);

int open (const char *file);
int filesize (int fd);

int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);

void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
int dup2 (int oldfd, int newfd);


tid_t fork (const char *thread_name);
//tid_t fork (const char *thread_name, struct intr_frame *f);
int exec (const char *file);

//file descripter
static struct file *find_file_by_fd(int fd);
int add_file_to_fdt(struct file *file);
void remove_file_from_fdt(int fd);

const int STDIN = 1;
const int STDOUT = 2;



/********************************************************/


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

	//system call file
	lock_init(&filesys_lock);
}






void check_addr(const uint64_t* tmp_addr){

	struct thread * cur = thread_current();
	// if(tmp_addr == NULL || !is_user_vaddr(tmp_addr) || pml4_get_page(cur->pml4, tmp_addr) == NULL)
	// 	exit(-1);

	if(pml4_get_page(cur->pml4, tmp_addr) == NULL)
		exit(-1);
}


static struct file *find_file_by_fd(int fd){
	if(fd<0 || fd >= FDCOUNT_LIMIT)return NULL;
	struct thread *cur = thread_current();
	return cur->fd_table[fd];
}

int add_file_to_fdt(struct file *file){
	struct thread *cur = thread_current();
	struct file **fdt = cur->fd_table;

	// while(cur->fd_idx < FDCOUNT_LIMIT && fdt[cur->fd_idx])
	// 	cur->fd_idx++;

	// if(cur->fd_idx >= FDCOUNT_LIMIT)return -1;
	// fdt[cur->fd_idx] = file;
	// return cur->fd_idx;
	for(int idx = cur->fd_idx; idx <FDCOUNT_LIMIT; idx++){
		if(fdt[idx] == NULL){
			fdt[idx] = file;
			cur->fd_idx = idx;
			return cur->fd_idx;
		}
	}
	cur->fd_idx = FDCOUNT_LIMIT;
	return -1;
}

void remove_file_from_fdt(int fd){
	if(fd<0 || fd >= FDCOUNT_LIMIT)return;
	struct thread *cur = thread_current();
	cur->fd_table[fd] = NULL;
}


void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	int syscall_num = f->R.rax; // rax: system call number
	switch(syscall_num){
		case SYS_HALT:                   /* Halt the operating system. */
			halt();
			break;
		case SYS_EXIT:                   /* Terminate this process. */
			exit(f->R.rdi);
			break;    
		case SYS_FORK:  ;                 /* Clone current process. */
			struct thread *curr = thread_current();
			memcpy(&curr->parent_if, f, sizeof(struct intr_frame));
			f->R.rax = fork(f->R.rdi);
			break;
		case SYS_EXEC:                   /* Switch current process. */
			if (exec(f->R.rdi) == -1)
				exit(-1);
			break;
		case SYS_WAIT:                   /* Wait for a child process to die. */
			f->R.rax = process_wait(f->R.rdi);
			break;
		case SYS_CREATE:                 /* Create a file. */
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:                 /* Delete a file. */
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:                   /* Open a file. */
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:               /* Obtain a file's size. */
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:                   /* Read from a file. */
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:                  /* Write to a file. */
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:                   /* Change position in a file. */
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:                   /* Report current position in a file. */
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:					 /* Close a file. */
			close(f->R.rdi);
			break;
		case SYS_DUP2:
			f->R.rax = dup2(f->R.rdi, f->R.rsi);
			break;
		default:						 /* call thread_exit() ? */
			exit(-1);
			break;
	}
	// printf ("system call!\n");
	// thread_exit ();
}



void
halt (void) {
	power_off();
}

void
exit (int status) {
	struct thread *t = thread_current();
	t->exit_status = status;
	printf("%s: exit(%d)\n", thread_name (), status);
	//printf("%s: exit(%d)\n", t->name, status);
	thread_exit();
}


bool
create (const char *file, unsigned initial_size) {
	check_addr(file);
	return filesys_create(file, initial_size);
}

bool
remove (const char *file) {
	check_addr(file);
	return filesys_remove(file);
}

int open(const char *file){
	check_addr(file);
	lock_acquire(&filesys_lock);
	struct file* fileobj = filesys_open(file);
	if(fileobj == NULL)return -1;

	int fd = add_file_to_fdt(fileobj);
	if(fd == -1)file_close(fileobj);
	lock_release(&filesys_lock);
	return fd;
}


void
close(int fd){
	// if(fd < 2 || fd >= FDCOUNT_LIMIT)return;
	// struct thread *cur = thread_current();
	// struct file *fileobj = cur->fd_table[fd];
	// if(fileobj == NULL)return;
	// //check_addr(fileobj);
	// cur->fd_table[fd] = NULL;

	if(fd<2)return;
	struct file *fileobj = find_file_by_fd(fd);
	if(fileobj == NULL)return;
	remove_file_from_fdt(fd);
	file_close(fileobj);
}


int
filesize (int fd) {
	// if(fd < 0 || fd > FDCOUNT_LIMIT) return -1;
	// struct thread *cur = thread_current();
	// struct file *fileobj = thread_current()->fd_table[fd];
	struct file *fileobj = find_file_by_fd(fd);
	if(fileobj == NULL)return -1;
	return file_length(fileobj);
}


int 
read (int fd, void *buffer, unsigned size){
	check_addr(buffer);
	//check_addr(buffer + size - 1);
	//if(fd<0 || fd>=FDCOUNT_LIMIT)return -1;
	

	struct file *tmpf = find_file_by_fd(fd);
	if(tmpf == NULL)return -1;
	if(tmpf == STDOUT)return -1;
	int readsize;//

	if(tmpf == STDIN){
		unsigned char * tmpbuf = buffer;
		for(readsize = 0; readsize<size; readsize++){
			char c = input_getc();
			//printf("!!!!!!%c\n", c);			//test
			*tmpbuf++ = c;
			if(c == '\0')
				break;
		}
	}
	else{
		//lock aquire
		lock_acquire(&filesys_lock);
		readsize = file_read(tmpf, buffer, size);
		lock_release(&filesys_lock);
	}
	return readsize;
}


int 
write (int fd, const void *buffer, unsigned size){
	check_addr(buffer);
	if(fd < 0 || fd >= FDCOUNT_LIMIT)return -1;
	int writesize;
	struct file *tmpf = find_file_by_fd(fd);
	if(tmpf == NULL || tmpf == STDIN)return -1;
	
	if(tmpf == STDOUT){
		putbuf(buffer, size);
		writesize = size;
	}
	else{
		lock_acquire(&filesys_lock);
		writesize = file_write(tmpf, buffer, size);
		lock_release(&filesys_lock);
	}

	return writesize;
}


tid_t
fork (const char *thread_name){
	struct thread* cur = thread_current();
	return process_fork(thread_name, &cur->parent_if);
}


int
exec (const char *file) {
	check_addr(file);
	int file_size = strlen(file) + 1;
	//file_length로 사용하면 문제가 발생하나?
	struct file *file_copy = palloc_get_page(PAL_ZERO);
	if(file_copy == NULL)exit(-1);
	strlcpy(file_copy, file, file_size);
	if(process_exec(file_copy) == -1)return -1;
	NOT_REACHED();
	return 0;

}




void
seek (int fd, unsigned position) {
	if(fd < 2 || fd > FDCOUNT_LIMIT)return;
	struct file *fileobj = thread_current()->fd_table[fd];
	if(fileobj == NULL)return;
	if(fileobj <= 2)return;
	//fileobj->pos = position;
	file_seek(fileobj, position);
}
	

unsigned
tell (int fd) {
	if(fd < 0 || fd > FDCOUNT_LIMIT)return -1;
	struct file *fileobj = thread_current()->fd_table[fd];
	if(fileobj < 2)return ;
	return file_tell(fileobj);
}

int
dup2 (int oldfd, int newfd){
	
}


/*********************************************/


