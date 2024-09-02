/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

//vm관련 추가 include
#include "threads/vaddr.h"

//?
#include "threads/mmu.h"

extern struct lock filesys_lock;	//syscall.h에 있던 lock을 여기에 가져왔다

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&lru);
	lock_init(&lru_lock);
	lock_init(&kill_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		struct page *pg = (struct page*)malloc(sizeof(struct page));
		if(pg == NULL)goto err;

		void *va_rounded = pg_round_down(upage);
		switch (VM_TYPE(type))
		{
		case VM_ANON:
			uninit_new(pg, va_rounded, init, type, aux, anon_initializer);
			break;
		case VM_FILE:
			uninit_new(pg, va_rounded, init, type, aux, file_backed_initializer);
			break;
		default:
		NOT_REACHED();
			break;
		}
		pg->writable = writable;
		
		/* TODO: Insert the page into the spt. */
		spt_insert_page(spt, pg);
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */
	//vm 관련 추가 사항
	//va가 void pointer이다. 정수 주소로 변환?
	void * page_addr = pg_round_down(va);
	//dummy page 만들기 가상주소만 쓰고 버릴거 -> free 해줘야 하지 않나?
	struct page pg;
	pg.va = page_addr;
	struct hash_elem *find_pg_in_hash = hash_find(&(spt->spt), &(pg.page_elem));

	if(find_pg_in_hash == NULL)return NULL;
	page = hash_entry(find_pg_in_hash, struct page, page_elem);

	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	//vm 관련 추가 사항
	if(hash_insert(&(spt->spt), &(page->page_elem)) == NULL)
		succ = true;
	//hash_insert에서 NULL이 아니면 이미 key값이 있어서 재할당이 들어간다.
	//즉 NULL일 경우 성공적인 insert이다.
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	if(hash_delete(&(spt->spt), &(page->page_elem)) == NULL)return;		//그런 page는 없어요일 경우
	vm_dealloc_page (page);
	return;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */
	 //LRU 순회하면서 가장 마지막에 방문했던 frame 찾아서 return
	 lock_acquire(&lru_lock);
	 struct list_elem *tmp = list_begin(&lru);
	 struct list_elem *nxt_tmp;
	 struct frame* tmp_frame;
	 for(size_t i = 0; i < list_size(&lru); i++){
		tmp_frame = list_entry(tmp, struct frame, lru_elem);
		if(pml4_is_accessed(thread_current()->pml4, tmp_frame->page->va)){
			pml4_set_accessed(thread_current()->pml4, tmp_frame->page->va, false);
			nxt_tmp = list_next(tmp);
			list_remove(tmp);
			list_push_back(&lru, tmp);
			tmp = nxt_tmp;
			continue;
		}
		if(victim == NULL){
			victim = tmp_frame;
			nxt_tmp = list_next(tmp);
			list_remove(tmp);
			tmp = nxt_tmp;
			continue;
		}
		tmp = list_next(tmp);
	 }
	 
	 if(victim == NULL)
			victim = list_entry(list_pop_front(&lru), struct frame, lru_elem);
	lock_release(&lru_lock);
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim  = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	//vm 관련 수정사항
	if(!swap_out(victim->page))return NULL;	
	victim->page = NULL;
	memset(victim->kva, 0, PGSIZE);

	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	//vm 관련 추가사항		새로운 frame 할당하고 초기화 한다.
	void *pg_ptr = palloc_get_page(PAL_USER);
	if(pg_ptr == NULL)return vm_evict_frame();

	frame = (struct frame*)malloc(sizeof(struct frame));	//new frame malloc으로 할당
	frame->kva = pg_ptr;
	frame->page = NULL;
	
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	void *pg_addr = pg_round_down(addr);
	if((uintptr_t)USER_STACK - (uintptr_t)pg_addr <= (1<<20))return;

	while(vm_alloc_page(VM_ANON, pg_addr, true)){
		struct page *pg = spt_find_page(&thread_current()->spt, pg_addr);
		vm_claim_page(pg_addr);
		pg_addr += PGSIZE;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
	void *parent_kva = page->frame->kva;
	page->frame->kva = palloc_get_page(PAL_USER);

	memcpy(page->frame->kva, parent_kva, PGSIZE);
	pml4_set_page(thread_current()->pml4, page->va, page->frame->kva, page->copy_writable);
	return true;
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if(is_kernel_vaddr(addr) && user)return false;
	page = spt_find_page(spt, addr);
	if(write && !not_present && page->copy_writable && page)
		return vm_handle_wp(page);

	if(page == NULL){
		struct thread *cur = thread_current();
		void *stack_bottom = pg_round_down(cur->user_rsp);
		if(write && (addr >= pg_round_down(cur->user_rsp - PGSIZE)) && (addr < USER_STACK)){
			vm_stack_growth(addr);
			return true;
		}
		return false;
	}
	
	// printf("page->writable : %d\n", page->writable);
	// printf("write : %d\n", write);
	// printf("EQUALS ? : %d\n", page->writable == write);
	// if (is_writable(thread_current()->pml4) && write)
	// {
	// 	printf("wp\n");
	// 	return vm_handle_wp(page);
	// };

	if(write && !page->writable)return false;
	if(vm_do_claim_page(page))return true;
	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	//vm 추가사항
	page = spt_find_page(&thread_current()->spt, va);
	if(page == NULL)return false;
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();
	//vm
	if(frame == NULL)return false;

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *t = thread_current();
	lock_acquire(&lru_lock);
	list_push_back(&lru, &frame->lru_elem);
	lock_release(&lru_lock);
	if(pml4_set_page(t->pml4, page->va, frame->kva, page->writable)==false)
		return false;
	return swap_in (page, frame->kva);
}

//vm 관련 추가 함수
//spt_hash_func
static uint64_t
spt_hash_func(const struct hash_elem *e){
	//page 불러와서 va를 hash 시킨다.
	const struct page *p = hash_entry(e, struct page, page_elem);
	return hash_int(p->va);
}
//vm 
//spt_less_func
static bool
spt_less_func(const struct hash_elem *a, const struct hash_elem *b){
	const struct page *pa = hash_entry(a, struct page, page_elem);
	const struct page *pb = hash_entry(b, struct page, page_elem);
	return pa->va < pb->va;
}



/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	//vm 관련 수정
	hash_init(&(spt->spt), spt_hash_func, spt_less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool 
supplemental_page_table_copy(struct supplemental_page_table *dst,
                                  struct supplemental_page_table *src)
{
  struct hash_iterator iter;
  hash_first(&iter, &(src->spt));
  while (hash_next(&iter))
  {
    struct page *tmp = hash_entry(hash_cur(&iter), struct page, page_elem);
    struct page *cpy = NULL;
    // printf("curr_type: %d, parent_va: %p, aux: %p\n", VM_TYPE(tmp->operations->type), tmp->va, tmp->uninit.aux);

    switch (VM_TYPE(tmp->operations->type))
    {
    	case VM_UNINIT:
      // printf("tmp->uninit.type: %d, va: %p, aux: %p\n", tmp->uninit.type, tmp->va, tmp->uninit.aux);
      	if (VM_TYPE(tmp->uninit.type) == VM_ANON)
      	{
        	struct load_segment_aux *info = (struct load_segment_aux *)malloc(sizeof(struct load_segment_aux));
        	memcpy(info, tmp->uninit.aux, sizeof(struct load_segment_aux));

        	info->file = file_duplicate(info->file);

        	vm_alloc_page_with_initializer(tmp->uninit.type, tmp->va, tmp->writable, tmp->uninit.init, (void *)info);
      	}
      	break;
    	case VM_ANON:
      // printf("VMANON\n");
      		vm_alloc_page(tmp->operations->type, tmp->va, tmp->writable);
      		cpy = spt_find_page(dst, tmp->va);

      // printf("child va : %p, type: %d\n", cpy->va, cpy->operations->type);

      		if (cpy == NULL)
      		{
        		return false;
      		}

      		cpy->copy_writable = tmp->writable;
      		struct frame *cpy_frame = malloc(sizeof(struct frame));
      		cpy->frame = cpy_frame;
      		cpy_frame->page = cpy;
    
      		cpy_frame->kva = tmp->frame->kva;

      		struct thread *t = thread_current();
      		lock_acquire(&lru_lock);
      		list_push_back(&lru, &cpy_frame->lru_elem);
      		lock_release(&lru_lock);

      		if (pml4_set_page(t->pml4, cpy->va, cpy_frame->kva, 0) == false)
      		{
        // printf("child set page flase \n");
        		return false;
      		}
      		swap_in(cpy, cpy_frame->kva);
      		break;
    	case VM_FILE:
      		break;
    	default:
      		break;
    }
  }
  return true;
}

//vm 추가 함수
static void spt_destroy_func(struct hash_elem *e, void *aux)
{
  	const struct page *pg = hash_entry(e, struct page, page_elem);
  	vm_dealloc_page(pg);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	lock_acquire(&kill_lock);
  	hash_destroy(&(spt->spt), spt_destroy_func);
  	lock_release(&kill_lock);

}
