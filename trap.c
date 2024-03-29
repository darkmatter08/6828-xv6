#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

// added because we removed static from vm.c:mappages() definition.
int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(proc->killed)
      exit();
    proc->tf = tf;
    syscall();
    if(proc->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpunum() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
      if (proc && (tf->cs & 3) == 3) {
        if (ticks - proc->alarmticks >= 10) {
          if(proc->alarmhandler != (void*) 0x0) {
            // before calling proc->alarmhandler, update proc->alarmticks
            // to the current value.
            proc->alarmticks = ticks;

            // cannot directly call!
            // executes with kernel priviliges!
            // proc->alarmhandler();

            // I believe the correct solution involves changing
            // proc->tf-> eip to point to the function stored in
            // proc->alarmhandler. We may also need to manually create
            // some space for it on the stack.
            // Eventually we'd want to save the registers as well.
            // This is really the same idea as creating a new process
            // and switching to it but instead we're doing it inside of a
            // trap frame.
            // save current eip from tf onto stack
            *(uint*)(proc->tf->esp) = proc->tf->eip;
            proc->tf->esp -= 4;
            // change current eip to alarmhandler.
            proc->tf->eip = (uint) proc->alarmhandler;
            // let code run and return.
            // What about switching back? restoring the eip?
            // Seems to work without that change.
          }
        }
      }
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpunum(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(proc == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpunum(), tf->eip, rcr2());
      panic("trap");
    }

    if (tf->trapno == T_PGFLT) {
      // attempt to alloc the page
      uint faulting_addr = rcr2();
      uint pg_start_boundary = PGROUNDDOWN(faulting_addr);
      // get some physical memory, in the form of a virtual addr in
      // the kernel space of virutal addresses.
      char* mem = kalloc();
      if(mem == 0){
        cprintf("kalloc() dynamic allocation failed!\n");
        // deallocuvm(pgdir, newsz, oldsz);
        // In user space, assume process misbehaved.
        cprintf("pid %d %s: trap %d err %d on cpu %d "
                "eip 0x%x addr 0x%x--kill proc\n",
                proc->pid, proc->name, tf->trapno, tf->err, cpunum(), tf->eip,
                rcr2());
        proc->killed = 1;
        return; // return code in eax
      }
      memset(mem, 0, PGSIZE);
      // now map V2P(mem) starting at the pg_start_boundary (VA) with some permissions
      if(mappages(proc->pgdir, (char*)pg_start_boundary, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
        cprintf("allocuvm out of memory (2)\n");
        // deallocuvm(pgdir, newsz, oldsz);
        kfree(mem);

        // In user space, assume process misbehaved.
        cprintf("pid %d %s: trap %d err %d on cpu %d "
                "eip 0x%x addr 0x%x--kill proc\n",
                proc->pid, proc->name, tf->trapno, tf->err, cpunum(), tf->eip,
                rcr2());
        proc->killed = 1;
        return;
      }
    }
    else {
      // some other weird fault has occured. 
      cprintf("unhandled fault\n");
      cprintf("pid %d %s: trap %d err %d on cpu %d "
                "eip 0x%x addr 0x%x--kill proc\n",
                proc->pid, proc->name, tf->trapno, tf->err, cpunum(), tf->eip,
                rcr2());
      proc->killed = 1;
    }
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(proc && proc->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(proc && proc->state == RUNNING && tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(proc && proc->killed && (tf->cs&3) == DPL_USER)
    exit();
}
