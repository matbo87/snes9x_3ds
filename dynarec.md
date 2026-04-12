# GSU dynarec

## Problems:
 - branch delay slot makes branches complicated with the presence of variable-size instructions,
   because branching to a block of code does not have consistent behavior
 - C is not capable of fully optimizing the code that we want. Assembly will be required.

## How to handle the JIT:
 - 'branch destination buffer': allocate one pointer for every possible GSU program counter address.
    - When loading a ROM, fill this buffer with pointers that lead the following routines:
      - ROM and GSU Cache: JIT and decode one instruction
      - RAM: Intepreter (we could also JIT RAM, but invalidation would be hard. Needs profiling.)
    - Then, the following events will trigger as follows:
      - GSU session begin: jump to the address in the buffer
      - Jump: jump to the address in the buffer
      - Branch: fall back to interpreter to handle the delay slot, then jump to the address in the buffer
        - This might be necessary for other events to handle ALT modes correctly

    - Branches can be decoded as follows by the JIT:
      - 1-byte delay slot: the delay slot can be decoded alongside the branch (will this break timings?)
      - Any other delay slot: fall back to interpreter
    - When running the JIT, each opcode decoded should have its location written back to this buffer
      - This must handle ALT modes. Maybe only branch when we have alt mode 0 set?

## How to handle code generation:
 - Each emitted instruction must:
   - Fetch pipe (if required by the instruction; currently done unconditionally by the interpreter loop)
   - Execute the instruction body
   - Decrement the instruction counter and branch to a return trampoline if the counter == 0
     - The speedhack can skip this step
     - These branches should be tagged UNLIKELY to help the predictor
     - The trampoline should be stored near enough to be a single branch, as we will probably want LR
       available for other uses
 - Almost no optimizations will be performed to keep code as general as possible, but we CAN optimize 
   based on the opcode byte itself, as it is an invariant. Namely, register indices/immediate values are
   constants that can be folded.

## Current questions:
 - How to handle ALT modes?
   - We could decode multiple codepaths for each branch destination, depending on the ALT mode. This
     would require effectively extending the address space by 4x, from 16K to 64K ARM pointers. Lots
     of empty space. Would the OS handle empty pages effectively in the TLB? Probably not, as we have
     no virtual memory.
   - We could run the interpreter until we reach ALT0, then branch to the branch destination buffer.
     This would only invoke a significant penalty for branch targets with ALT modes set, which is
     probably not thaaaat common.
 - How do we handle banking? Does the SNES even use banks, and would they matter here?
 - How bad will register pressure be?
   - Probably pretty gnarly...
 - How will we access variables that aren't held in registers?
   - We could put everything we could possibly need inside of the GSU struct to avoid ever needing
     to load from elsewhere. This would simplify codegen, since all code would essentially be relocatable
   - We could break code into multiple buffers, with data embedded at the end. This is probably ideal
     for memory usage, since we'd only allocate what's necessary. On the other hand, memory is cheap...
   - We could embed data sporadically in a large buffer, branching over it when necessary. GCC sometimes
     does this, but it would require alignment and would increase code size slightly.
