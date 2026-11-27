/* pagefault_handler.c  --  interrupt 14, ECE565
 *
 * Two outcomes only:
 *
 *   the faulting page was vmalloc'd  ->  lazy allocation: install a
 *                                        table if needed, grab an FFS
 *                                        frame, map it, return
 *
 *   anything else                    ->  P<pid>:: SEGMENTATION_FAULT
 *                                        and the process is killed
 *
 * There is no backing store and no replacement policy in this lab;
 * the spec guarantees FFS never runs out.
 *
 * The handler runs on the faulting process's stack with interrupts
 * disabled by the dispatcher.  Its first act is kmap_enter(), because
 * a user directory maps neither FFS nor page-table memory, and the
 * handler has to write both.  Its own code, stack and the tables it
 * reads all live below XINU_PAGES, which every directory maps, so the
 * CR3 switch is safe mid-function.
 */

#include <xinu.h>

void pagefault_handler(void) {
	pid32           pid   = currpid;
	struct procent *prptr = &proctab[pid];
	uint32          vaddr = (uint32)read_cr2();
	uint32          saved, pdbr, vpage, di, ti, ptframe, frame;
	pd_t           *pd;
	pt_t           *pt;

	saved = kmap_enter();

	pdbr  = prptr->pdbr;
	vpage = vaddr / PAGE_SIZE;
	di    = vaddr >> 22;
	ti    = (vaddr >> 12) & 0x3FF;

	/* ---- is this address legal for this process? ---------------- */

	if (prptr->prisuser == FALSE ||
	    prptr->prvmap   == NULL  ||
	    pdbr == 0 ||
	    vpage <  VHEAP_BASE_PAGE ||
	    vpage >= VHEAP_BASE_PAGE + MAX_HEAP_PAGES ||
	    bm_test(prptr->prvmap, vpage - VHEAP_BASE_PAGE) == 0) {

		kprintf("P%d:: SEGMENTATION_FAULT\n", pid);
		kmap_exit(saved);
		kill(pid);
		return;                 /* not reached when killing self */
	}

	/* ---- lazy allocation --------------------------------------- */

	pd = (pd_t *)pdbr;

	if (pd[di].pd_pres == 0) {              /* no table yet */
		ptframe = get_pt_frame();
		if (ptframe == 0) {
			kprintf("P%d:: page table space exhausted\n", pid);
			kmap_exit(saved);
			kill(pid);
			return;
		}
		pd[di].pd_pres  = 1;
		pd[di].pd_write = 1;
		pd[di].pd_user  = 0;
		pd[di].pd_base  = ptframe / PAGE_SIZE;
	}

	pt = (pt_t *)(pd[di].pd_base * PAGE_SIZE);

	if (pt[ti].pt_pres) {                   /* already mapped */
		kmap_exit(saved);
		invlpg(vaddr);                  /* stale TLB entry */
		return;
	}

	frame = get_ffs_frame(pid, vpage * PAGE_SIZE);
	if (frame == 0) {
		kprintf("P%d:: out of FFS frames\n", pid);
		kmap_exit(saved);
		kill(pid);
		return;
	}

	pt[ti].pt_pres  = 1;
	pt[ti].pt_write = 1;
	pt[ti].pt_user  = 0;
	pt[ti].pt_base  = frame / PAGE_SIZE;

	kmap_exit(saved);
	invlpg(vaddr);
}