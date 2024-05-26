/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */
#include <bitmap.h>

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

/** Project 3: Swap In/Out - 한 페이지를 섹터 단위로 관리 */
#define SECTOR_SIZE (PGSIZE / DISK_SECTOR_SIZE)
size_t swap_size;
struct bitmap *swap_table;

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
	swap_size = disk_size(swap_disk) / SECTOR_SIZE;

	/* 페이지 단위로 swap in out 진행하므로 페이지 수만큼 비트를 생성해줌. */
	swap_table = bitmap_create(swap_size);
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

	/** Project 3: Swap In/Out - ERROR로 초기화  */
    anon_page->sector = BITMAP_ERROR;

    return true;
}

/** Project 3: Swap In/Out - Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in(struct page *page, void *kva) {
    struct anon_page *anon_page = &page->anon;
    size_t sector = anon_page->sector;
    size_t slot = sector / SECTOR_SIZE;

    if (sector == BITMAP_ERROR || !bitmap_test(swap_table, slot))
        return false;

    bitmap_set(swap_table, slot, false);

    for (size_t i = 0; i < SECTOR_SIZE; i++)
        disk_read(swap_disk, sector + i, kva + DISK_SECTOR_SIZE * i);

    sector = BITMAP_ERROR;

    return true;
}

/** Project 3: Swap In/Out - Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	size_t free_idx = bitmap_scan_and_flip (swap_table, 0, 1, false);

	if (free_idx == BITMAP_ERROR)
		return false;

	// disk는 sector단위로 관리
	// os가 관리하는 비트맵은 sector 8개 단위로 관리
	size_t sector = free_idx * SECTOR_SIZE;

	for (size_t i = 0; i < SECTOR_SIZE; i++)
		disk_write(swap_disk, sector + i, page->va + DISK_SECTOR_SIZE * i);

	anon_page->sector = sector;

	// disk에 기록했으니 pte 정보는 삭제해서 메모리 효율을 높여야 함
    uint64_t pte = pml4e_walk(thread_current()->pml4, page->va, false);
	if (((uint64_t)pte) & PTE_P)
		palloc_free_page((void *)PTE_ADDR(pte));

	pml4_clear_page(thread_current()->pml4, page->va);
    if (page->frame){
		// 기존에 있던 프레임의 page, frame 간의 맵핑을 지우고
		// 새로운 page를 frame에 이후에 할당해줘야함
		// 그러니 해당 프레임 메모리를 없애면 안됨
		// list_remove(&page->frame->frame_elem);
		page->frame->page = NULL;
		// free(page->frame);
		page->frame = NULL;
	}


	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;

    /** Project 3: Swap In/Out - 점거중인 bitmap 삭제 */
    if (anon_page->sector != BITMAP_ERROR)
        bitmap_reset(swap_table, anon_page->sector / SECTOR_SIZE);


    /** Project 3: Anonymous Page - 점거중인 frame 삭제 */
    if (page->frame) {
		list_remove(&page->frame->frame_elem);
        page->frame->page = NULL;
        free(page->frame);
        page->frame = NULL;
    }

	pml4_clear_page(thread_current()->pml4, page->va);
}
