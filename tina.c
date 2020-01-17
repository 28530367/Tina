#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "tina.h"

// TODO: Are there any relevant ABIs that aren't 16 byte aligned, downward moving stacks?
// TODO: Is it worthwhile to try and detect stack overflows?

tina* tina_init_stack(tina* coro, tina_func* body, void** sp_loc, void* sp);
uintptr_t tina_swap(tina* coro, uintptr_t value, void** sp);

tina* tina_init(void* buffer, size_t size, tina_func* body, void* user_data){
	tina* coro = buffer;
	(*coro) = (tina){.user_data = user_data, .running = true};
	return tina_init_stack(coro, body, &coro->_sp, buffer + size);
}

uintptr_t tina_yield(tina* coro, uintptr_t value){
	return tina_swap(coro, value, &coro->_sp);
}

void tina_context(tina* coro, tina_func* body){
	// Yield back to the tina_init_stack() call, and return the coroutine.
	uintptr_t value = tina_yield(coro, (uintptr_t)coro);
	// Call the body function with the first value.
	value = body(coro, value);
	// body() has exited, and the coroutine is finished.
	coro->running = false;
	// Yield the final return value back to the calling thread.
	tina_yield(coro, value);
	
	// Any attempt to resume the coroutine after it's finished should call the error func.
	while(true){
		if(coro->error_handler) coro->error_handler(coro, "Attempted to resume a dead coroutine.");
		tina_yield(coro, 0);
	}
}

#if __amd64 && __GNUC__
	#if __linux__ || __APPLE__
	#define TINA_USE_SYSVAMD64
	#else
	#error Unknown system.
	#endif

	#ifdef TINA_USE_SYSVAMD64
		#define ARG0 "rdi"
		#define ARG1 "rsi"
		#define ARG2 "rdx"
		#define ARG3 "rcx"
		#define RET "rax"
	#elif TINA_USE_WIN64
		// TODO look this up.
	#else
		#error Unknown amd64 ABI?
		#define TINA_NO_ASM
	#endif

	#ifndef TINA_NO_ASM
		asm(".intel_syntax noprefix");

		asm(".func tina_init_stack");
		asm("tina_init_stack:");
		// Save the caller's registers and stack pointer.
		// tina_yield() will restore them once the coroutine is primed.
		asm("  push rbp");
		asm("  push rbx");
		asm("  push r12");
		asm("  push r13");
		asm("  push r14");
		asm("  push r15");
		asm("  mov ["ARG2"], rsp");
		// Align and apply the coroutine's stack.
		asm("  and "ARG3", ~0xF");
		asm("  mov rsp, "ARG3"");
		// Now executing within the new coroutine's stack!
		// When tina_context() first calls tina_yield() it will
		// return back to where tina_init_stack() was called.

		// Push an NULL activation record onto the stack to make debuggers happy.
		asm("  push 0");
		asm("  push 0");
		// Tail call to tina_context() to finish the coroutine initialization.
		asm("  jmp tina_context");
		asm(".endfunc");

		asm(".func tina_swap");
		asm("tina_swap:");
		// Preserve calling coroutine's registers.
		asm("  push rbp");
		asm("  push rbx");
		asm("  push r12");
		asm("  push r13");
		asm("  push r14");
		asm("  push r15");
		// Swap stacks.
		asm("  mov rax, rsp");
		asm("  mov rsp, ["ARG2"]");
		asm("  mov ["ARG2"], rax");
		// Restore callee coroutine's registers.
		asm("  pop r15");
		asm("  pop r14");
		asm("  pop r13");
		asm("  pop r12");
		asm("  pop rbx");
		asm("  pop rbp");
		// return 'value' to the callee.
		asm("  mov "RET", "ARG1"");
		asm("  ret");
		asm(".endfunc");

		asm(".att_syntax");
	#endif
#else
	#error Unknown CPU/compiler combo.
#endif
