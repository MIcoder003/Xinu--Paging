/* vmalloc.c  --  private virtual heap allocation, ECE565 (first fit)
 *
 * Allocation is lazy: vmalloc marks pages in the process's bitmap and
 * returns an address.  No FFS frame is reserved until the page is
 * touched and the fault handler maps it.
 *
 * It does check page-table availability up front, though.  A request
 * can fit in virtual space and still be impossible because mapping it
 * would need more second-level tables than MAX_PT_SIZE has left, and
 * the large test cases distinguish those two failures by message.
 *
 * Allocations are page aligned and page granular, per the simplifying
 * assumptions, so the bitmap is the whole allocator.
 */

#include <xinu.h>

/* ------------------------------------------------------------------ *
 * First fit over the heap bitmap.  Returns the starting page index,   *
 * or MAX_HEAP_PAGES if no run of npages free pages exists.           *
 *                                                                     *
 * Whole free words are skipped 32 pages at a time, which matters: the  *
 * heap is a million pages wide and a bit-at-a-time scan of it runs on  *
 * every allocation.                                                   *
 * ------------------------------------------------------------------ */

static uint32 first_fit(uint32 *map, uint32 npages) {
	uint32 i = 0, run = 0;

	while (i < MAX_HEAP_PAGES) {
		if ((i & 31) == 0 && (i + 32) <= MAX_HEAP_PAGES &&
		    map[bm_word(i)] == 0) {
			run += 32;		/* whole word free */
			i   += 32;
		} else if (bm_test(map, i)) {
			run = 0;
			i++;
		} else {
			run++;
			i++;
		}

		if (run >= npages) {
			return i - npages;	/* i is one past the run */
		}
	}
	return MAX_HEAP_PAGES;			/* no fit */
}

/* How many second-level tables this range still needs.  Counts only
 * directory entries that are not already present.  Must run with the
 * system directory loaded -- a user directory does not map PT space. */

static uint32 tables_needed(uint32 pdbr, uint32 start, uint32 npages) {
	pd_t  *pd = (pd_t *)pdbr;
	uint32 first = (VHEAP_BASE_PAGE + start) / NPTE;
	uint32 last  = (VHEAP_BASE_PAGE + start + npages - 1) / NPTE;
	uint32 di, need = 0;

	for (di = first; di <= last && di < NPTE; di++) {
		if (pd[di].pd_pres == 0) {
			need++;
		}
	}
	return need;
}

/* ------------------------------------------------------------------ *
 * vmalloc  --  reserve nbytes of private heap, first fit from low to   *
 * high addresses.  Returns a virtual address, or SYSERR.             *
 * ------------------------------------------------------------------ */

char *vmalloc(uint32 nbytes) {
	intmask         mask;
	struct procent *prptr;
	uint32          npages, start, need, saved, i;

	mask  = disable();
	prptr = &proctab[currpid];

	/* only vcreate()d processes have a private heap */
	if (prptr->prisuser == FALSE || prptr->prvmap == NULL || nbytes == 0) {
		restore(mask);
		return (char *)SYSERR;
	}

	npages = (nbytes + PAGE_SIZE - 1) / PAGE_SIZE;

	/* more virtual space than the heap holds at all */
	if (npages > MAX_HEAP_PAGES) {
		kprintf("vmalloc(): not enough contiguous heap space available!\n");
		restore(mask);
		return (char *)SYSERR;
	}

	/* ECE565 first fit.  The hole this leaves behind when a smaller
	 * allocation reuses a larger freed block is the external
	 * fragmentation the spec warns about, and it is intended.      */
	start = first_fit(prptr->prvmap, npages);
	if (start >= MAX_HEAP_PAGES) {
		kprintf("vmalloc(): not enough contiguous heap space available!\n");
		restore(mask);
		return (char *)SYSERR;
	}

	/* Room in virtual space is not enough: the range also has to be
	 * mappable, and second-level tables come out of MAX_PT_SIZE.   */
	saved = kmap_enter();
	need  = tables_needed(prptr->pdbr, start, npages);
	kmap_exit(saved);

	if (need > free_pt_frames()) {
		kprintf("vmalloc(): not enough page table space available!\n");
		restore(mask);
		return (char *)SYSERR;
	}

	for (i = start; i < start + npages; i++) {
		bm_set(prptr->prvmap, i);
	}
	prptr->prvpages += npages;

	restore(mask);
	return (char *)(VHEAP_BASE_ADDR + (uint32)start * PAGE_SIZE);
}

/* ------------------------------------------------------------------ *
 * vfree  --  release heap space previously returned by vmalloc.       *
 *                                                                     *
 * ECE565: a bad call must free nothing.  So validate every page first  *
 * and only then start releasing.  Page tables are left in place --     *
 * they go back only when the process dies.                           *
 * ------------------------------------------------------------------ */

syscall vfree(char *ptr, uint32 nbytes) {
	intmask         mask;
	struct procent *prptr;
	uint32          addr = (uint32)ptr;
	uint32          npages, start, i, saved;

	mask  = disable();
	prptr = &proctab[currpid];

	if (prptr->prisuser == FALSE || prptr->prvmap == NULL || nbytes == 0) {
		restore(mask);
		return SYSERR;
	}

	/* must be a page-aligned address inside this process's heap */
	if ((addr & (PAGE_SIZE - 1)) != 0 || addr < VHEAP_BASE_ADDR) {
		restore(mask);
		return SYSERR;
	}

	start  = (addr - VHEAP_BASE_ADDR) / PAGE_SIZE;
	npages = (nbytes + PAGE_SIZE - 1) / PAGE_SIZE;

	if (start >= MAX_HEAP_PAGES || npages > MAX_HEAP_PAGES ||
	    start + npages > MAX_HEAP_PAGES) {
		restore(mask);
		return SYSERR;
	}

	/* validate the whole range BEFORE touching anything */
	for (i = 0; i < npages; i++) {
		if (bm_test(prptr->prvmap, start + i) == 0) {
			restore(mask);
			return SYSERR;		/* never allocated */
		}
	}

	/* release.  unmap_page walks page tables, so take the system
	 * directory first.                                            */
	saved = kmap_enter();
	for (i = 0; i < npages; i++) {
		unmap_page(prptr->pdbr,
		           VHEAP_BASE_ADDR + (uint32)(start + i) * PAGE_SIZE);
		bm_clear(prptr->prvmap, start + i);
	}
	kmap_exit(saved);

	prptr->prvpages -= npages;
	restore(mask);
	return OK;
}