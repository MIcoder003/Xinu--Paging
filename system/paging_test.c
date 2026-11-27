/* paging_test.c  --  staged tests for the ECE565 paging lab
 *
 * Follows the spec's testing plan.  Enable one stage at a time, in
 * order; do not move on until the current stage is clean.
 *
 *   A  paging on, system processes only
 *   B  vcreate / kill
 *   C  vmalloc / vfree, no heap access, no handler needed
 *   D  heap access -> lazy allocation, and the segfault path
 *   E  several user processes, isolation and context switch
 *
 * Call paging_test() from main().
 */

#include <xinu.h>

/* #define STAGE_A */
/* #define STAGE_B */
/* #define STAGE_C */
/* #define STAGE_D */
#define STAGE_E

static int passed = 0;
static int failed = 0;

static void check(int cond, char *name) {
	if (cond) {
		passed++;
		kprintf("  PASS  %s\n", name);
	} else {
		failed++;
		kprintf("  FAIL  %s\n", name);
	}
}

static int pt_free_count(void) {
	int n = 0;
	int i = pt_head;
	while (i != -1) {
		n++;
		i = pt_tab[i].next;
	}
	return n;
}

/* What the hardware would compute for vaddr under pdbr.              */
#define BAD_WALK 0xFFFFFFFF

static uint32 walk(uint32 pdbr, uint32 vaddr) {
	pd_t  *pd = (pd_t *)pdbr;
	pt_t  *pt;
	uint32 di  = vaddr >> 22;
	uint32 ti  = (vaddr >> 12) & 0x3FF;
	uint32 off = vaddr & 0xFFF;

	if (pdbr == 0 || pd[di].pd_pres == 0) {
		return BAD_WALK;
	}
	pt = (pt_t *)(pd[di].pd_base * PAGE_SIZE);
	if (pt[ti].pt_pres == 0) {
		return BAD_WALK;
	}
	return (pt[ti].pt_base * PAGE_SIZE) + off;
}

/* ================================================================== *
 * STAGE A -- paging on, system processes only                         *
 * ================================================================== */

#ifdef STAGE_A
static void stage_a(void) {
	uint32 pdbr = system_pdbr;
	pd_t  *pd   = (pd_t *)pdbr;
	pt_t  *pt;
	uint32 v, p;
	int    i, j, nbad, nmissing;

	kprintf("\n=== STAGE A: system address space ===\n");
	kprintf("paging is on and kprintf still works -- the kernel\n");
	kprintf("static segments are mapped.\n");
	kprintf("  KERNEL_PT=%d  MAX_GLOBAL_PT=%d  IDENTITY_PAGES=%d (%d MB)\n",
	        KERNEL_PT, MAX_GLOBAL_PT, IDENTITY_PAGES,
	        (IDENTITY_PAGES * 4) / 1024);
	kprintf("  heap base  = 0x%08x (PDE %d)\n",
	        VHEAP_BASE_ADDR, VHEAP_BASE_PAGE / NPTE);

	check(sizeof(pd_t) == 4, "sizeof(pd_t) == 4");
	check(sizeof(pt_t) == 4, "sizeof(pt_t) == 4");
	if (failed) {
		kprintf("  bitfield structs are wrong; stop here.\n");
		return;
	}

	check(pdbr != 0 && (pdbr & 0xFFF) == 0, "system pdbr is page aligned");
	check(proctab[NULLPROC].pdbr == pdbr, "NULLPROC uses the system directory");
	check(proctab[currpid].pdbr == pdbr, "this system process shares it");
	check(proctab[currpid].prisuser == FALSE, "create() marks system procs");

	/* directory */
	nbad = nmissing = 0;
	for (i = 0; i < MAX_GLOBAL_PT; i++) {
		if (pd[i].pd_pres == 0) {
			nmissing++;
		}
		if (pd[i].pd_base != global_pt[i] / PAGE_SIZE) {
			nbad++;
		}
	}
	check(nmissing == 0, "all MAX_GLOBAL_PT kernel PDEs present");
	check(nbad == 0, "pd_base holds frame numbers, not addresses");

	nbad = 0;
	for (i = MAX_GLOBAL_PT; i < NPTE; i++) {
		if (pd[i].pd_pres) {
			nbad++;
		}
	}
	check(nbad == 0, "PDEs past the identity window are absent");

	/* tables */
	nbad = 0;
	for (i = 0; i < MAX_GLOBAL_PT; i++) {
		pt = (pt_t *)global_pt[i];
		for (j = 0; j < NPTE; j++) {
			if (pt[j].pt_pres == 0 ||
			    pt[j].pt_base != (uint32)(i * NPTE + j)) {
				nbad++;
			}
		}
	}
	check(nbad == 0, "every kernel PTE identity-maps its own page");

	/* translation */
	nbad = 0;
	for (i = 0; i < IDENTITY_PAGES; i++) {
		v = (uint32)i * PAGE_SIZE;
		if (walk(pdbr, v) != v) {
			if (nbad == 0) {
				kprintf("      first mismatch at 0x%08x\n", v);
			}
			nbad++;
		}
	}
	check(nbad == 0, "identity window translates to itself");

	v = (uint32)(XINU_PAGES + 7) * PAGE_SIZE + 0x123;
	check(walk(pdbr, v) == v, "byte offset within a page survives");
	check(walk(pdbr, VHEAP_BASE_ADDR) == BAD_WALK, "heap base is unmapped");
	check(walk(pdbr, 0xC0000000) == BAD_WALK, "0xC0000000 is unmapped");

	/* frame pools */
	kprintf("      PT free %d/%d   FFS free %d/%d\n",
	        pt_free_count(), MAX_PT_SIZE,
	        free_ffs_pages(), MAX_FFS_SIZE);
	check(pt_free_count() == MAX_PT_SIZE - (MAX_GLOBAL_PT + 1),
	      "PT pool spent MAX_GLOBAL_PT tables + 1 directory");
	check(free_ffs_pages() == MAX_FFS_SIZE, "FFS pool untouched");
	check(used_ffs_frames(currpid) == 0, "no FFS frames charged to us");

	/* one line per PDE -- the dump the spec asks for */
	p = 0;
	(void)p;
	dump_pd(pdbr, KERNEL_PT + 2);
}
#endif

/* ================================================================== *
 * STAGE B -- vcreate and kill                                         *
 * ================================================================== */

#if defined(STAGE_B) || defined(STAGE_C) || defined(STAGE_D) || defined(STAGE_E)
static void user_idle(void) {
	while (TRUE) {
		sleepms(50);
	}
}
#endif

#ifdef STAGE_B
static void stage_b(void) {
	pid32  u;
	uint32 upd;
	pd_t  *pd;
	int    pt_before, ffs_before, i, nbad;

	kprintf("\n=== STAGE B: vcreate / kill ===\n");
	pt_before  = pt_free_count();
	ffs_before = free_ffs_pages();

	u = vcreate((void *)user_idle, 4096, 20, "uidle", 0, NULL);
	check(u != SYSERR, "vcreate returned a pid");
	if (u == SYSERR) {
		return;
	}

	upd = proctab[u].pdbr;
	kprintf("      user pdbr = 0x%08x   system pdbr = 0x%08x\n",
	        upd, system_pdbr);

	check(upd != 0, "user process has a directory");
	check(upd != system_pdbr, "it is not the shared system directory");
	check(proctab[u].prisuser == TRUE, "vcreate marks it a user process");
	check(proctab[u].prvmap != NULL, "heap bitmap was allocated");
	check(allocated_virtual_pages(u) == 0, "no heap pages allocated yet");
	check(pt_free_count() == pt_before - 1, "one PT frame spent on the PD");

	/* isolation: kernel tables shared, everything else absent */
	pd = (pd_t *)upd;
	nbad = 0;
	for (i = 0; i < KERNEL_PT; i++) {
		if (pd[i].pd_pres == 0 ||
		    pd[i].pd_base != global_pt[i] / PAGE_SIZE) {
			nbad++;
		}
	}
	check(nbad == 0, "user PD shares the kernel tables");

	nbad = 0;
	for (i = KERNEL_PT; i < NPTE; i++) {
		if (pd[i].pd_pres) {
			nbad++;
		}
	}
	check(nbad == 0, "FFS and PT space are NOT mapped for the user proc");

	check(walk(upd, (uint32)FFS_BASE_PAGE * PAGE_SIZE) == BAD_WALK,
	      "user cannot reach FFS space");
	check(walk(upd, (uint32)PT_BASE_PAGE * PAGE_SIZE) == BAD_WALK,
	      "user cannot reach page table space");
	check(walk(upd, 0x00100000) == 0x00100000,
	      "user can still reach kernel text");

	resume(u);
	sleepms(100);
	kill(u);
	sleepms(100);

	check(pt_free_count() == pt_before, "kill() returned the PD and tables");
	check(free_ffs_pages() == ffs_before, "kill() returned every FFS frame");
	check(proctab[u].prvmap == NULL, "kill() freed the bitmap");
}
#endif

/* ================================================================== *
 * STAGE C -- vmalloc / vfree with no heap access                      *
 * ================================================================== */

#ifdef STAGE_C
static void user_c(pid32 parent) {
	char *a, *b, *c;

	a = vmalloc(PAGE_SIZE);
	check(a != (char *)SYSERR, "vmalloc of one page succeeds");
	check((uint32)a == VHEAP_BASE_ADDR, "first page is at the heap base");

	b = vmalloc(2 * PAGE_SIZE);
	check((uint32)b == VHEAP_BASE_ADDR + PAGE_SIZE,
	      "second allocation follows the first");
	check(allocated_virtual_pages(currpid) == 3, "three pages allocated");
	check(used_ffs_frames(currpid) == 0,
	      "lazy: no FFS frame reserved at allocation time");

	/* page granularity: 1 byte still costs a whole page */
	c = vmalloc(1);
	check((uint32)c == VHEAP_BASE_ADDR + 3 * PAGE_SIZE,
	      "a 1-byte request takes a full page");
	check(allocated_virtual_pages(currpid) == 4, "four pages allocated");

	/* bad frees must change nothing */
	check(vfree(c + PAGE_SIZE, PAGE_SIZE) == SYSERR,
	      "vfree of a never-allocated page is SYSERR");
	check(vfree((char *)(VHEAP_BASE_ADDR - PAGE_SIZE), PAGE_SIZE) == SYSERR,
	      "vfree below the heap base is SYSERR");
	check(allocated_virtual_pages(currpid) == 4,
	      "a failed vfree freed nothing");

	/* a partly-valid range must free nothing (ECE565) */
	check(vfree(c, 2 * PAGE_SIZE) == SYSERR,
	      "vfree spanning allocated and unallocated pages is SYSERR");
	check(allocated_virtual_pages(currpid) == 4,
	      "the partly-valid vfree freed nothing");

	check(vfree(a, PAGE_SIZE) == OK, "vfree of a valid page succeeds");
	check(allocated_virtual_pages(currpid) == 3, "count dropped by one");

	/* first fit: the hole at the base is reused before higher space */
	a = vmalloc(PAGE_SIZE);
	check((uint32)a == VHEAP_BASE_ADDR, "first fit reuses the low hole");

	check(vfree(b, 2 * PAGE_SIZE) == OK, "freeing the 2-page block works");
	check(vfree(a, PAGE_SIZE) == OK, "freeing the low page works");
	check(vfree(c, PAGE_SIZE) == OK, "freeing the last page works");
	check(allocated_virtual_pages(currpid) == 0, "heap is empty again");

	send(parent, 0);
	while (TRUE) {
		sleepms(100);
	}
}

static void stage_c(void) {
	pid32 u;
	int   ffs_before;

	kprintf("\n=== STAGE C: vmalloc / vfree, no access ===\n");
	ffs_before = free_ffs_pages();

	recvclr();
	u = vcreate((void *)user_c, 8192, 20, "uc", 1, currpid);
	if (u == SYSERR) {
		check(FALSE, "vcreate for stage C");
		return;
	}
	resume(u);
	check(recvtime(3000) != TIMEOUT, "stage C process finished");
	kill(u);
	sleepms(100);
	check(free_ffs_pages() == ffs_before, "no FFS frames leaked");
}
#endif

/* ================================================================== *
 * STAGE D -- heap access and the segfault path                        *
 * ================================================================== */

#ifdef STAGE_D
static void user_d(pid32 parent) {
	char  *p;
	uint32 i;
	int    ok;

	p = vmalloc(4 * PAGE_SIZE);
	check(p != (char *)SYSERR, "vmalloc of four pages");
	check(used_ffs_frames(currpid) == 0, "nothing mapped before access");

	/* first touch of page 0 */
	p[0] = 'A';
	check(used_ffs_frames(currpid) == 1, "first touch mapped one frame");
	check(p[0] == 'A', "the write stuck");

	/* same page again: no new frame */
	p[PAGE_SIZE - 1] = 'B';
	check(used_ffs_frames(currpid) == 1, "same page costs no second frame");

	/* touch the other three */
	p[PAGE_SIZE]     = 'C';
	p[2 * PAGE_SIZE] = 'D';
	p[3 * PAGE_SIZE] = 'E';
	check(used_ffs_frames(currpid) == 4, "four frames for four pages");

	/* a fresh page must read back as zero */
	ok = 1;
	for (i = 1; i < PAGE_SIZE - 1; i++) {
		if (p[2 * PAGE_SIZE + i] != 0) {
			ok = 0;
			break;
		}
	}
	check(ok, "a newly mapped page is zero filled");

	/* data survives across pages */
	check(p[0] == 'A' && p[PAGE_SIZE] == 'C' &&
	      p[2 * PAGE_SIZE] == 'D' && p[3 * PAGE_SIZE] == 'E',
	      "all four pages hold their data");

	/* vfree releases the frames */
	check(vfree(p, 4 * PAGE_SIZE) == OK, "vfree of the touched range");
	check(used_ffs_frames(currpid) == 0, "vfree released all four frames");

	send(parent, 0);
	while (TRUE) {
		sleepms(100);
	}
}

/* This one must die.  It touches heap space it never allocated. */
static void user_segv(pid32 parent) {
	char *p = (char *)(VHEAP_BASE_ADDR + 16 * PAGE_SIZE);

	*p = 'X';                       /* expect SEGMENTATION_FAULT */
	send(parent, 0);                /* must never run */
	while (TRUE) {
		sleepms(100);
	}
}

static void stage_d(void) {
	pid32 u;
	int   ffs_before;

	kprintf("\n=== STAGE D: lazy allocation ===\n");
	ffs_before = free_ffs_pages();

	recvclr();
	u = vcreate((void *)user_d, 8192, 20, "ud", 1, currpid);
	resume(u);
	check(recvtime(3000) != TIMEOUT, "stage D process finished");
	kill(u);
	sleepms(100);
	check(free_ffs_pages() == ffs_before, "no FFS frames leaked");

	kprintf("\n--- segmentation fault (one line below is expected) ---\n");
	recvclr();
	u = vcreate((void *)user_segv, 4096, 20, "usegv", 1, currpid);
	resume(u);
	check(recvtime(2000) == TIMEOUT, "the faulting process was killed");
	check(proctab[u].prstate == PR_FREE, "its proctab entry was released");
	check(free_ffs_pages() == ffs_before, "its frames came back");
	check(pt_free_count() == MAX_PT_SIZE - (MAX_GLOBAL_PT + 1),
	      "its page tables came back");
}
#endif

/* ================================================================== *
 * STAGE E -- several user processes                                   *
 * ================================================================== */

#ifdef STAGE_E
static void user_e(pid32 parent, char tag) {
	char  *p;
	uint32 i;
	int    ok = 1;

	p = vmalloc(2 * PAGE_SIZE);
	for (i = 0; i < 2 * PAGE_SIZE; i += PAGE_SIZE) {
		p[i] = tag;
	}

	/* give the other process a chance to run and write its own tag */
	for (i = 0; i < 20; i++) {
		sleepms(10);
		if (p[0] != tag || p[PAGE_SIZE] != tag) {
			ok = 0;
			break;
		}
	}

	check(ok, "a process's heap pages were not disturbed by the other");
	check((uint32)p == VHEAP_BASE_ADDR,
	      "both processes see the same virtual heap base");

	send(parent, 0);
	while (TRUE) {
		sleepms(100);
	}
}

static void stage_e(void) {
	pid32  a, b;
	uint32 pa, pb;
	int    ffs_before;

	kprintf("\n=== STAGE E: two user processes ===\n");
	ffs_before = free_ffs_pages();

	recvclr();
	a = vcreate((void *)user_e, 8192, 20, "ua", 2, currpid, 'a');
	b = vcreate((void *)user_e, 8192, 20, "ub", 2, currpid, 'b');
	check(a != SYSERR && b != SYSERR, "both user processes created");
	check(proctab[a].pdbr != proctab[b].pdbr, "distinct page directories");

	resume(a);
	resume(b);
	check(recvtime(4000) != TIMEOUT, "first process finished");
	check(recvtime(4000) != TIMEOUT, "second process finished");

	/* same virtual address, different physical frames */
	pa = walk(proctab[a].pdbr, VHEAP_BASE_ADDR);
	pb = walk(proctab[b].pdbr, VHEAP_BASE_ADDR);
	kprintf("      0x%08x maps to 0x%08x in A, 0x%08x in B\n",
	        VHEAP_BASE_ADDR, pa, pb);
	check(pa != BAD_WALK && pb != BAD_WALK, "both mapped the heap base");
	check(pa != pb, "the same virtual address uses different frames");

	check(used_ffs_frames(a) == 2, "process A holds two frames");
	check(used_ffs_frames(b) == 2, "process B holds two frames");

	kill(a);
	kill(b);
	sleepms(200);
	check(free_ffs_pages() == ffs_before, "both processes cleaned up");
	check(pt_free_count() == MAX_PT_SIZE - (MAX_GLOBAL_PT + 1),
	      "all page tables returned");
}
#endif

/* ================================================================== */

void paging_test(void) {
	passed = 0;
	failed = 0;

#ifdef STAGE_A
	stage_a();
#endif
#ifdef STAGE_B
	stage_b();
#endif
#ifdef STAGE_C
	stage_c();
#endif
#ifdef STAGE_D
	stage_d();
#endif
#ifdef STAGE_E
	stage_e();
#endif

	kprintf("\n==============================================\n");
	kprintf("passed %d, failed %d\n", passed, failed);
}