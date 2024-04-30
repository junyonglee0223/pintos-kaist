/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
//vm 추가 include
#include "threads/mmu.h"

//vm추가 lock
extern struct lock filesys_lock;
extern struct lock lru_lock;

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}


//vm mmap 추가 함수
static bool
lazy_mmap(struct page *page, void *aux){
	bool success = true;
	struct mmap_aux *info = (struct mmap*)aux;
	list_push_back(&(thread_current()->mmap_list), &(page->file.file_elem));
	
	//lock extern 방식으로 정의를 어디서 해야할 지 아직 모름...
	lock_acquire(&filesys_lock);
	off_t read = file_read_at(info->file, page->va, (off_t)info->page_read_bytes, info->ofs);
	lock_release(&filesys_lock);
	if(read != (off_t)info->page_read_bytes){
		vm_dealloc_page(page);
		success = false;
	}
	else{
		memset((page->va) + info->page_read_bytes, 0, info->page_read_bytes);
		page->file.page = page;
		page->file.file = info->file;
		page->file.start = info->start;
		page->file.length = info->length;
		page->file.ofs = info->ofs;
		page->file.page_read_bytes = info->page_read_bytes;
		page->file.page_zero_bytes = info->page_zero_bytes;
	}
	free(aux);
	pml4_set_dirty(thread_current()->pml4, page->va, false);
	return success;
}


/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
			//vm 추가사항
	struct file *reopen_file = file_reopen(file);
	if(reopen_file == NULL)return NULL;
	//int i = 0;
	size_t read_bytes = length;
	size_t zero_bytes = PGSIZE - length % PGSIZE;
	off_t dynamic_ofs = offset;
	void *upage = addr;
	while(read_bytes > 0 || zero_bytes > 0){
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct mmap_aux * aux = (struct mmap_aux *)malloc(sizeof(struct mmap_aux));
		aux->file = reopen_file;
		aux->start = addr;
		aux->length = length;
		aux->ofs = dynamic_ofs;
		aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;
		if(!vm_alloc_page_with_initializer(VM_FILE, upage, writable, lazy_mmap, (void*)aux)){
			file_close(reopen_file);
			return NULL;
		}
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		dynamic_ofs += PGSIZE;
		//i++;
	}
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct thread *cur = thread_current();
	struct page *pg = spt_find_page(&(cur->spt), addr);
	size_t length = pg->file.length;
	struct file *file = pg->file.file;

	//void *tmp;
	size_t pivot = 0;
	while(pivot < length){
		pg = spt_find_page(&(cur->spt), addr);
		if(pml4_is_dirty(cur->pml4, addr)){
			lock_acquire(&filesys_lock);
			file_write_at(file, addr, pg->file.page_read_bytes, pg->file.ofs);
			lock_release(&filesys_lock);
		}

		hash_delete(&(cur->spt), &(pg->page_elem));
		spt_remove_page(&cur->spt, pg);
		vm_dealloc_page(pg);

		addr += PGSIZE;
		pivot += PGSIZE;
	}
	file_close(file);
}
