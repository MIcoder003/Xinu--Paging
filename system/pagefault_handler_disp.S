/* pagefault_handler_disp.S  --  interrupt 14 dispatcher
 *
 * YOUR LAB SHIPS THIS FILE.  Use the provided one.  This is a reference
 * copy, to compare against if the handler never runs or the machine
 * triple faults on return.
 *
 * On a page fault with no privilege change the CPU has pushed, from
 * the top of the stack down:
 *
 *   [esp +  0]  error code      <- pushed by the CPU, must be removed
 *   [esp +  4]  EIP
 *   [esp +  8]  CS
 *   [esp + 12]  EFLAGS
 *
 * iret does not pop the error code, so the dispatcher has to.  CR2
 * still holds the faulting address when pagefault_handler runs.
 */

		.text
		.globl	pagefault_handler_disp
		.globl	pagefault_handler

pagefault_handler_disp:
		cli
		pushal				/* save the process's registers	*/
		call	pagefault_handler
		popal
		addl	$4, %esp		/* discard the error code	*/
		iret