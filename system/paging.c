/* paging.c  --  ECE565 demand-paged virtual memory
 *
 * Physical memory is split into three regions by paging.h:
 *
 *   pages 0 .. XINU_PAGES-1          kernel text, data and heap
 *   pages FFS_BASE_PAGE ..           free frame space (user heap pages)
 *   pages PT_BASE_PAGE ..            page directories and page tables
 *
 * Every page directory maps the first region, so kernel code and data
 * are reachable from any address space.  Only the SYSTEM directory
 * maps the other two: requirements 4 and 5 say a user process must not
 * be able to see another process's FFS frames, or any page table.
 *
 * Consequence: any kernel code that touches a page table or zeroes an
 * FFS frame has to run with the system directory loaded.  Bracket it
 * with kmap_enter() / kmap_exit().
 */

#include <xinu.h>

struct ffs_frame ffs_tab[MAX_FFS_SIZE];
int ffs_head = 0;

pt_frame pt_tab[MAX_PT_SIZE];
int pt_head = 0;

uint32 global_pt[MAX_GLOBAL_PT];        /* the shared kernel tables   */
uint32 system_pdbr = 0;                 /* directory for system procs */

/* ------------------------------------------------------------------ *
 * Free frame space                                                   *
 * ------------------------------------------------------------------ */

void init_ffs(void) {
	int i;
	for (i = 0; i < MAX_FFS_SIZE; i++) {
		ffs_tab[i].status = FFS_FREE;
		ffs_tab[i].pid    = BADPID;
		ffs_tab[i].vaddr  = 0;
		ffs_tab[i].next   = (i == MAX_FFS_SIZE - 1) ? -1 : (i + 1);
	}
	ffs_head = 0;
}

/* Hand out one zeroed FFS frame and record who owns it and at which
 * virtual address.  Returns a physical address, or 0 if the pool is
 * empty.  Frame 0 is always kernel, so 0 is a safe sentinel.         */

uint32 get_ffs_frame(pid32 pid, uint32 vaddr) {
	int    idx;
	uint32 paddr;

	if (ffs_head == -1) {
		return 0;
	}

	idx      = ffs_head;
	ffs_head = ffs_tab[idx].next;

	ffs_tab[idx].status = FFS_USED;
	ffs_tab[idx].pid    = pid;
	ffs_tab[idx].vaddr  = vaddr;
	ffs_tab[idx].next   = -1;

	paddr = (uint32)(FFS_BASE_PAGE + idx) * PAGE_SIZE;
	memset((void *)paddr, 0, PAGE_SIZE);
	return paddr;
}

void free_ffs_frame(uint32 paddr) {
	int idx = (int)(paddr / PAGE_SIZE) - FFS_BASE_PAGE;

	if (idx < 0 || idx >= MAX_FFS_SIZE) {
		return;                         /* not an FFS address */
	}
	if (ffs_tab[idx].status == FFS_FREE) {
		return;                         /* double free */
	}

	ffs_tab[idx].status = FFS_FREE;
	ffs_tab[idx].pid    = BADPID;
	ffs_tab[idx].vaddr  = 0;
	ffs_tab[idx].next   = ffs_head;
	ffs_head = idx;
}

/* ------------------------------------------------------------------ *
 * Page directory / page table space                                  *
 * ------------------------------------------------------------------ */

void init_pt(void) {
	int i;
	for (i = 0; i < MAX_PT_SIZE; i++) {
		pt_tab[i].status = PG_FREE;
		pt_tab[i].next   = (i == MAX_PT_SIZE - 1) ? -1 : (i + 1);
	}
	pt_head = 0;
}

uint32 get_pt_frame(void) {
	int    idx;
	uint32 paddr;

	if (pt_head == -1) {
		return 0;
	}

	idx     = pt_head;
	pt_head = pt_tab[idx].next;
	pt_tab[idx].status = PG_USED;
	pt_tab[idx].next   = -1;

	paddr = (uint32)(PT_BASE_PAGE + idx) * PAGE_SIZE;
	memset((void *)paddr, 0, PAGE_SIZE);    /* every entry not-present */
	return paddr;
}

void free_pt_frame(uint32 paddr) {
	int idx = (int)(paddr / PAGE_SIZE) - PT_BASE_PAGE;

	if (idx < 0 || idx >= MAX_PT_SIZE) {
		return;
	}
	if (pt_tab[idx].status == PG_FREE) {
		return;
	}

	pt_tab[idx].status = PG_FREE;
	pt_tab[idx].next   = pt_head;
	pt_head = idx;
}

/* ------------------------------------------------------------------ *
 * Address space helpers                                              *
 * ------------------------------------------------------------------ */

void invlpg(uint32 vaddr) {
	asm volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

/* Load the system directory so FFS and page-table memory is reachable.
 * Returns the CR3 to hand back to kmap_exit().                       */

uint32 kmap_enter(void) {
	uint32 saved = (uint32)read_cr3();

	if (saved != system_pdbr) {
		write_cr3((unsigned long)system_pdbr);
	}
	return saved;
}

void kmap_exit(uint32 saved) {
	if (saved != (uint32)read_cr3()) {
		write_cr3((unsigned long)saved);
	}
}

/* ------------------------------------------------------------------ *
 * The shared kernel page tables.  Table i identity-maps virtual pages *
 * [i*1024, i*1024+1023] onto the frames of the same number.          *
 * ------------------------------------------------------------------ */

void init_kernel_pt(void) {
	int    i, j;
	uint32 frame;
	pt_t  *pt;

	for (i = 0; i < MAX_GLOBAL_PT; i++) {
		frame = get_pt_frame();
		if (frame == 0) {
			kprintf("init_kernel_pt: out of PT frames at %d\n", i);
			panic("init_kernel_pt");
		}

		global_pt[i] = frame;
		pt = (pt_t *)frame;

		for (j = 0; j < NPTE; j++) {
			pt[j].pt_pres  = 1;
			pt[j].pt_write = 1;
			pt[j].pt_user  = 0;
			pt[j].pt_base  = (uint32)(i * NPTE + j);
		}
	}
}

/* Build a page directory whose first ntables entries point at the
 * shared kernel tables.  ntables is MAX_GLOBAL_PT for the system
 * directory, KERNEL_PT for a user directory.  Every other entry stays
 * zero, so touching it faults.  Returns a physical address, or 0.    */

uint32 create_pd(uint32 ntables) {
	uint32 pdaddr;
	pd_t  *pd;
	uint32 i;

	if (ntables > MAX_GLOBAL_PT) {
		ntables = MAX_GLOBAL_PT;
	}

	pdaddr = get_pt_frame();                /* already zeroed */
	if (pdaddr == 0) {
		return 0;
	}

	pd = (pd_t *)pdaddr;
	for (i = 0; i < ntables; i++) {
		pd[i].pd_pres  = 1;
		pd[i].pd_write = 1;
		pd[i].pd_user  = 0;
		pd[i].pd_base  = global_pt[i] / PAGE_SIZE;
	}
	return pdaddr;
}

/* Tear down a USER directory: release every FFS frame still mapped in
 * it, every second-level table it owns, then the directory itself.
 * ECE565 requirement 5.  The shared kernel tables are never freed.
 *
 * Caller must hold the system directory, or be killing the process
 * that owns pdbr -- see the CR3 juggling below.                      */

void free_pd(uint32 pdbr) {
	uint32 saved;
	pd_t  *pd;
	pt_t  *pt;
	int    i, j;

	if (pdbr == 0 || pdbr == system_pdbr) {
		return;                         /* never free the shared one */
	}

	saved = kmap_enter();
	pd = (pd_t *)pdbr;

	for (i = KERNEL_PT; i < NPTE; i++) {
		if (pd[i].pd_pres == 0) {
			continue;
		}
		pt = (pt_t *)(pd[i].pd_base * PAGE_SIZE);
		for (j = 0; j < NPTE; j++) {
			if (pt[j].pt_pres) {
				free_ffs_frame(pt[j].pt_base * PAGE_SIZE);
				pt[j].pt_pres = 0;
			}
		}
		pd[i].pd_pres = 0;
		free_pt_frame((uint32)pt);
	}
	free_pt_frame(pdbr);

	/* If we just freed the directory CR3 pointed at, leave the system
	 * directory loaded; resched() installs the next process's.       */
	if (saved != pdbr) {
		kmap_exit(saved);
	}
}

/* Drop one heap page: release its FFS frame and clear the PTE.  The
 * second-level table is left in place -- the spec says vfree releases
 * heap space only.  Caller must already be in kmap_enter().          */

void unmap_page(uint32 pdbr, uint32 vaddr) {
	pd_t  *pd = (pd_t *)pdbr;
	pt_t  *pt;
	uint32 di = vaddr >> 22;
	uint32 ti = (vaddr >> 12) & 0x3FF;

	if (pdbr == 0 || pd[di].pd_pres == 0) {
		return;                         /* never faulted in */
	}
	pt = (pt_t *)(pd[di].pd_base * PAGE_SIZE);
	if (pt[ti].pt_pres == 0) {
		return;                         /* allocated, never touched */
	}

	free_ffs_frame(pt[ti].pt_base * PAGE_SIZE);
	pt[ti].pt_pres = 0;
	pt[ti].pt_base = 0;
	invlpg(vaddr);
}

/* ------------------------------------------------------------------ *
 * Bring-up.  Call from the end of sysinit(), after the NULLPROC entry *
 * is filled in and after set_evec(14, ...).                          *
 * ------------------------------------------------------------------ */

void init_paging(void) {
	unsigned long cr0;

	init_ffs();
	init_pt();
	init_kernel_pt();

	/* one directory shared by NULLPROC and every create()d process */
	system_pdbr = create_pd(MAX_GLOBAL_PT);
	if (system_pdbr == 0) {
		panic("init_paging: cannot build the system page directory");
	}

	proctab[NULLPROC].pdbr     = system_pdbr;
	proctab[NULLPROC].prisuser = FALSE;
	proctab[NULLPROC].prvmap   = NULL;
	proctab[NULLPROC].prvpages = 0;

	write_cr3((unsigned long)system_pdbr);

	cr0 = read_cr0();
	cr0 |= 0x80000000;                      /* CR0.PG */
	write_cr0(cr0);
}

/* ------------------------------------------------------------------ *
 * Debug utilities required by the spec                               *
 * ------------------------------------------------------------------ */

uint32 free_ffs_pages(void) {
	uint32 n = 0;
	int    i = ffs_head;

	while (i != -1) {
		n++;
		i = ffs_tab[i].next;
	}
	return n;
}

/* How many page-directory / page-table frames are left.  vmalloc needs
 * this to tell "no virtual space" from "no page table space".      */

uint32 free_pt_frames(void) {
	uint32 n = 0;
	int    i = pt_head;

	while (i != -1) {
		n++;
		i = pt_tab[i].next;
	}
	return n;
}

uint32 allocated_virtual_pages(pid32 pid) {
	if (isbadpid(pid) || proctab[pid].prstate == PR_FREE) {
		return 0;
	}
	/* The spec's expected output counts the process's whole virtual
	 * address space, not just its heap: a process that has allocated
	 * nothing still reports VHEAP_BASE_PAGE (25600 here), and every
	 * later figure is that baseline plus the heap pages it holds. */
	return VHEAP_BASE_PAGE + proctab[pid].prvpages;
}

uint32 used_ffs_frames(pid32 pid) {
	uint32 n = 0;
	int    i;

	for (i = 0; i < MAX_FFS_SIZE; i++) {
		if (ffs_tab[i].status == FFS_USED && ffs_tab[i].pid == pid) {
			n++;
		}
	}
	return n;
}

/* Readable dump of a directory and its non-kernel tables.  Testing
 * plan step A asks for this -- it is the only way to debug a hang at
 * startup.                                                           */

void dump_pd(uint32 pdbr, uint32 nentries) {
	uint32 saved;
	pd_t  *pd;
	pt_t  *pt;
	uint32 i, j, mapped;

	if (pdbr == 0) {
		kprintf("dump_pd: null pdbr\n");
		return;
	}

	saved = kmap_enter();
	pd = (pd_t *)pdbr;
	kprintf("page directory at 0x%08x\n", pdbr);

	for (i = 0; i < nentries && i < NPTE; i++) {
		if (pd[i].pd_pres == 0) {
			continue;
		}
		pt = (pt_t *)(pd[i].pd_base * PAGE_SIZE);
		mapped = 0;
		for (j = 0; j < NPTE; j++) {
			if (pt[j].pt_pres) {
				mapped++;
			}
		}
		kprintf("  PDE[%4d] -> pt 0x%08x  va 0x%08x  %d/%d mapped%s\n",
		        i, (uint32)pt, i << 22, mapped, NPTE,
		        (i < KERNEL_PT) ? "  (kernel, shared)" : "");
	}
	kmap_exit(saved);
}