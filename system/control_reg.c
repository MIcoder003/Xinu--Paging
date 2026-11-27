/* control_reg.c  --  x86 control register accessors
 *
 * The stock paging.h already declares these with "unsigned long"
 * parameters.  Match that exactly or the build fails on conflicting
 * types.  If your tree already ships a control_reg.c with these
 * defined, delete this file instead -- you only need one copy.
 */

#include <xinu.h>

unsigned long read_cr0(void) {
	unsigned long val;
	asm volatile("movl %%cr0, %0" : "=r"(val));
	return val;
}

void write_cr0(unsigned long n) {
	asm volatile("movl %0, %%cr0" : : "r"(n));
}

unsigned long read_cr2(void) {
	unsigned long val;
	asm volatile("movl %%cr2, %0" : "=r"(val));
	return val;
}

unsigned long read_cr3(void) {
	unsigned long val;
	asm volatile("movl %%cr3, %0" : "=r"(val));
	return val;
}

void write_cr3(unsigned long n) {
	asm volatile("movl %0, %%cr3" : : "r"(n));
}

unsigned long read_cr4(void) {
	unsigned long val;
	asm volatile("movl %%cr4, %0" : "=r"(val));
	return val;
}

void write_cr4(unsigned long n) {
	asm volatile("movl %0, %%cr4" : : "r"(n));
}