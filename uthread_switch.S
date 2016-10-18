	.text

/* Switch from current_thread to next_thread. Make next_thread
 * the current_thread, and set next_thread to 0.
 * Use eax as a temporary register, which should be caller saved.
 */
	.globl thread_switch
thread_switch:
	/* YOUR CODE HERE */
	// load current_thread->sp into %esp
	// pushal
	// copy new %esp into current_thread->sp

	movl current_thread, %eax;
	// movl (%eax), %esp;
	pushal;
	movl %esp, (%eax);

	// load next_thread->sp into %esp
	// popal
	// copy new %esp into next_thread->sp

	movl next_thread, %eax;
	movl (%eax), %esp;
	popal;
	movl %esp, (%eax);

	// current_thread = next_thread
	movl next_thread, %eax;
	movl %eax, current_thread;
	// next_thread = 0
	movl $0, next_thread;

	// ASK do I have to set this up? It looks at the location of esp
	ret;				/* pop return address from stack */
