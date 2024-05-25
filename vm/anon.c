/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */
#include <bitmap.h>

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"

/** Project 3: Swap In/Out - 한 페이지를 섹터 단위로 관리 */
#define SECTOR_SIZE (PGSIZE / DISK_SECTOR_SIZE)


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

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1,1);
    /* disk_size는 섹터를 반환함. 섹터 1개당 512byte임.
	 * 우리는 페이지 단위로 swap in out을 진행할 것임. 
	 * 섹터 8개 = 페이지 이므로 8로 나누면 swap_size는 페이지 단위 개수로 환산됨. */
	size_t swap_size = disk_size(swap_disk) / SECTOR_SIZE;

	/* 페이지 단위로 swap in out 진행하므로 페이지 수만큼 비트를 생성해줌. */
    struct bitmap *swap_table = bitmap_create(swap_size);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	/* 데이터를 모두 0으로 초기화 */
	struct uninit_page *uninit = &page->uninit;
    memset(uninit, 0, sizeof(struct uninit_page));

	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
}

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

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;

    /** Project 3: Anonymous Page - 점거중인 frame 삭제 */
    if (page->frame) {
		list_remove(&page->frame->frame_elem);
        page->frame->page = NULL;
        free(page->frame);
        page->frame = NULL;
    }
}
