/* paging.h  --  ECE565 demand-paged virtual memory
 *
 * Complete replacement.  Back up your original first and diff it:
 *
 *   cp paging.h paging.h.bak
 *   # install this file, then:
 *   diff paging.h.bak paging.h
 *
 * The values below match what your own stage-A run reported
 * (IDENTITY_PAGES 25600, MAX_GLOBAL_PT 25, FFS 16384, PT 1024).  If the
 * diff shows a constant, struct field or prototype in your original
 * that is missing here, paste it back in -- the lab's own test-case
 * files may reference it.
 */

#ifndef _PAGING_H_
#define _PAGING_H_

/* ------------------------------------------------------------------ *
 * 1.  Physical memory layout                                          *
 *                                                                     *
 *   page 0            kernel text, data, heap      XINU_PAGES frames  *
 *   FFS_BASE_PAGE     free frame space             MAX_FFS_SIZE       *
 *   PT_BASE_PAGE      page directories and tables  MAX_PT_SIZE        *
 *                                                                     *
 * MAX_FFS_SIZE and MAX_PT_SIZE are always multiples of 1024, and      *
 * everything derived below follows from them, so the code does not    *
 * care what they are set to.                                          *
 * ------------------------------------------------------------------ */

#ifndef PAGE_SIZE
#define PAGE_SIZE	4096
#endif

#define XINU_PAGES	8192		/* 32 MB of kernel space	*/
#define MAX_FFS_SIZE	16384		/* 64 MB of free frame space	*/
#define MAX_PT_SIZE	1024		/* 4 MB of PD + PT space	*/

/* the spec spells it both ways; keep an alias for the test cases */
#ifndef MAX_FSS_SIZE
#define MAX_FSS_SIZE	MAX_FFS_SIZE
#endif

#define NPTE		1024		/* entries in a directory/table	*/

/* ------------------------------------------------------------------ *
 * 2.  Hardware page directory and page table entries                  *
 * ------------------------------------------------------------------ */

typedef struct {
	unsigned int pd_pres	: 1;	/* page table present		*/
	unsigned int pd_write	: 1;	/* 1 = read/write		*/
	unsigned int pd_user	: 1;	/* 0 = supervisor only		*/
	unsigned int pd_pwt	: 1;	/* write through		*/
	unsigned int pd_pcd	: 1;	/* cache disable		*/
	unsigned int pd_acc	: 1;	/* accessed			*/
	unsigned int pd_mbz	: 1;	/* must be zero			*/
	unsigned int pd_fmb	: 1;	/* 4 MB pages; must be zero	*/
	unsigned int pd_global	: 1;	/* global (ignored)		*/
	unsigned int pd_avail	: 3;	/* free for software use	*/
	unsigned int pd_base	: 20;	/* frame number of the table	*/
} pd_t;

typedef struct {
	unsigned int pt_pres	: 1;	/* page present			*/
	unsigned int pt_write	: 1;	/* 1 = read/write		*/
	unsigned int pt_user	: 1;	/* 0 = supervisor only		*/
	unsigned int pt_pwt	: 1;	/* write through		*/
	unsigned int pt_pcd	: 1;	/* cache disable		*/
	unsigned int pt_acc	: 1;	/* accessed			*/
	unsigned int pt_dirty	: 1;	/* dirty			*/
	unsigned int pt_mbz	: 1;	/* must be zero			*/
	unsigned int pt_global	: 1;	/* global (ignored)		*/
	unsigned int pt_avail	: 3;	/* free for software use	*/
	unsigned int pt_base	: 20;	/* frame number of the page	*/
} pt_t;

/* ------------------------------------------------------------------ *
 * 3.  Frame pool bookkeeping                                          *
 * ------------------------------------------------------------------ */

#define FFS_FREE	0
#define FFS_USED	1

#define PG_FREE		0
#define PG_USED		1

#ifndef BADPID
#define BADPID		(-1)		/* no owning process		*/
#endif

/* One entry per FFS frame.  pid and vaddr record who the frame was
 * handed to and at which virtual address, so a frame can be traced
 * back to the PTE that maps it.					*/

struct ffs_frame {
	int	status;			/* FFS_FREE or FFS_USED		*/
	pid32	pid;			/* owner, or BADPID		*/
	uint32	vaddr;			/* virtual address it backs	*/
	int	next;			/* next free index, or -1	*/
};

/* One entry per page-directory / page-table frame.		*/

typedef struct {
	int	status;			/* PG_FREE or PG_USED		*/
	int	next;			/* next free index, or -1	*/
} pt_frame;

/* ------------------------------------------------------------------ *
 * 4.  Derived layout.  Nothing here is hand-tuned.                    *
 * ------------------------------------------------------------------ */

#define FFS_BASE_PAGE	(XINU_PAGES)
#define PT_BASE_PAGE	(XINU_PAGES + MAX_FFS_SIZE)
#define IDENTITY_PAGES	(XINU_PAGES + MAX_FFS_SIZE + MAX_PT_SIZE)

/* Tables mapping the kernel static segments.  Shared by EVERY page
 * directory, user directories included.			*/
#define KERNEL_PT	((XINU_PAGES + NPTE - 1) / NPTE)

/* Tables covering the whole identity window.  SYSTEM directory only:
 * requirements 4 and 5 forbid mapping FFS or PD/PT into user space. */
#define MAX_GLOBAL_PT	((IDENTITY_PAGES + NPTE - 1) / NPTE)

/* A user address space holds the kernel window and nothing else, so the
 * private heap starts immediately above it -- page 8192, 0x02000000.
 * That is a page-directory boundary, so a heap page never shares a
 * table with a kernel page.  FFS and PD/PT space are absent from a
 * user address space entirely (requirements 4 and 5): their physical
 * addresses are reachable only through the system directory.         */
#define VHEAP_BASE_PAGE	(KERNEL_PT * NPTE)
#define VHEAP_BASE_ADDR	((uint32)VHEAP_BASE_PAGE * PAGE_SIZE)

/* The heap spans everything above the kernel window -- the rest of the
 * 4 GB space, 1040384 pages.  It is virtual space, not frames, so it
 * is far larger than FFS; the large test cases allocate 65536 pages at
 * a time and probe this exact boundary.			*/
#define MAX_HEAP_PAGES	((NPTE * NPTE) - VHEAP_BASE_PAGE)
#define VMAP_WORDS	((MAX_HEAP_PAGES + 31) / 32)
#define VMAP_BYTES	(VMAP_WORDS * 4)

/* ------------------------------------------------------------------ *
 * 5.  Heap page bitmap.  One bit per virtual heap page: set means the  *
 * page was vmalloc'd.  A fault on a clear bit is a segmentation       *
 * fault; a fault on a set bit is lazy allocation.                     *
 * ------------------------------------------------------------------ */

#define bm_word(n)	((n) >> 5)
#define bm_mask(n)	(1 << ((n) & 31))
#define bm_test(m,n)	((((m)[bm_word(n)]) & bm_mask(n)) != 0)
#define bm_set(m,n)	((m)[bm_word(n)] |= bm_mask(n))
#define bm_clear(m,n)	((m)[bm_word(n)] &= ~bm_mask(n))

/* ------------------------------------------------------------------ *
 * 6.  Globals                                                         *
 * ------------------------------------------------------------------ */

extern struct ffs_frame	ffs_tab[];
extern int		ffs_head;
extern pt_frame		pt_tab[];
extern int		pt_head;

extern uint32		global_pt[MAX_GLOBAL_PT];  /* shared kernel tables */
extern uint32		system_pdbr;		   /* directory for sys procs */

/* ------------------------------------------------------------------ *
 * 7.  Control registers (control_reg.c)                               *
 * ------------------------------------------------------------------ */

unsigned long	read_cr0(void);
unsigned long	read_cr2(void);
unsigned long	read_cr3(void);
unsigned long	read_cr4(void);
void		write_cr0(unsigned long n);
void		write_cr3(unsigned long n);
void		write_cr4(unsigned long n);

/* ------------------------------------------------------------------ *
 * 8.  Prototypes                                                      *
 * ------------------------------------------------------------------ */

/* paging.c */
extern void	init_ffs(void);
extern void	init_pt(void);
extern void	init_kernel_pt(void);
extern void	init_paging(void);
extern uint32	get_ffs_frame(pid32 pid, uint32 vaddr);
extern void	free_ffs_frame(uint32 paddr);
extern uint32	get_pt_frame(void);
extern void	free_pt_frame(uint32 paddr);
extern uint32	create_pd(uint32 ntables);
extern void	free_pd(uint32 pdbr);
extern void	unmap_page(uint32 pdbr, uint32 vaddr);
extern uint32	kmap_enter(void);
extern void	kmap_exit(uint32 saved);
extern void	invlpg(uint32 vaddr);
extern void	dump_pd(uint32 pdbr, uint32 nentries);

/* debug utilities required by the spec */
extern uint32	free_ffs_pages(void);
extern uint32	free_pt_frames(void);
extern uint32	allocated_virtual_pages(pid32 pid);
extern uint32	used_ffs_frames(pid32 pid);

/* vmalloc.c */
extern char	*vmalloc(uint32 nbytes);
extern syscall	vfree(char *ptr, uint32 nbytes);

/* vcreate.c */
extern pid32	vcreate(void *funcaddr, uint32 ssize, pri16 priority,
			char *name, uint32 nargs, ...);

/* pagefault_handler.c and pagefault_handler_disp.S */
extern void	pagefault_handler(void);
extern void	pagefault_handler_disp(void);

/* paging_test.c */
extern void	paging_test(void);

#endif /* _PAGING_H_ */