/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include <bitmap.h>

#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};


static struct bitmap *disk_bitmap;
static struct lock bitmap_lock;
extern struct list lru;
extern struct lock lru_lock;

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = NULL;
	disk_bitmap = bitmap_create((size_t)disk_bitmap);
	lock_init(&bitmap_lock);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	//vm 관련 anon 추가 사항
	anon_page->sec_no = SIZE_MAX;
	anon_page->thread = thread_current();
	return true;
}


/***************************************test code spttablecopy ***************************************/

void m_anon_destroy(struct page *page){
	/*TODO find fram which connect to page*/
	struct frame *frame = page->frame;

	/*TODO find frame in lru list and remove it and carful to use lock
	and free frame*/
	lock_acquire(&lru_lock);
	list_remove(&frame->lru_elem);
	lock_release(&lru_lock);

	free(frame);

	/*TODO if anon page secno is not SEC_MAX
	use bitmap_set_multiple(disk_bitmap, anon_secno, 8, false)*/
	if(page->anon.sec_no != SIZE_MAX){
		bitmap_set_multiple(disk_bitmap, page->anon.sec_no, 8, false);
	}
}


/***************************************test code spttablecopy ***************************************/

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}

// /* Destroy the anonymous page. PAGE will be freed by the caller. */
// static void
// anon_destroy (struct page *page) {
// 	struct anon_page *anon_page = &page->anon;
// }


/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
    struct anon_page *anon_page = &page->anon;
    if (page->frame != NULL)
    {
        //printf("!!!!!!!!!!!!!!!!anon_destroy: %s!!!!!!!!!!!!!!!!\n", thread_current()->name);
        // printf("remove: %p, kva:%p\n", page->va, page->frame->kva);
        // printf("list_size: %d, list: %p\n", list_size(&lru), &lru);

        lock_acquire(&lru_lock);
        list_remove(&page->frame->lru_elem);
        lock_release(&lru_lock);

        // printf("anon_destroy: list: %p\n", &lru);

        // pte write bit 1 -> free
        free(page->frame);
    }
    if (anon_page->sec_no != SIZE_MAX)
        bitmap_set_multiple(disk_bitmap, anon_page->sec_no, 8, false);
}