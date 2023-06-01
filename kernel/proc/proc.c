#include "proc.h"

#include "free_proc_pool.h"
#include "kernel/fs/fs.h"
#include "kernel/fs/log.h"
#include "kernel/mem/kalloc.h"
#include "kernel/mem/memlayout.h"
#include "kernel/mem/vm.h"
#include "kernel/printf.h"
#include "kernel/util/string.h"
#include "kernel/util/vector.h"
#include "kstack_provider.h"
#include "trap.h"

struct cpu cpus[NCPU];

// We don't need to sync when accessing size, it only grows
// This is a vector, we lock it only when adding a process or accessing an
// element
struct {
  struct vector proc;
  struct spinlock proc_lock;
} proc_list;

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[];  // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

pagetable_t k_pagetable;

void proc_list_init() {
  v_init(&proc_list.proc);
  initlock(&proc_list.proc_lock, "proc lock");
}

// Find first free space in a proc list and put a proc ptr there.
// So we won't skip a lot of free space in our functions
// Returns a position where ptr is places, or -1 on failure
int push_proc(struct proc *p) {
  acquire(&proc_list.proc_lock);
  int pos = v_replace_first_zero(&proc_list.proc, (uint64)p);
  release(&proc_list.proc_lock);
  return pos;
}

// Remove proc from a proc list
void remove_proc_from_list(struct proc *p) {
  if (p->list_index == -1) return;
  acquire(&proc_list.proc_lock);
  v_set(&proc_list.proc, p->list_index, 0);
  release(&proc_list.proc_lock);
}

// Get i-th element from a proc list, and if it exists increase p->watching
// while holding a lock, so we'll avoid NPE
// Elements of the proc list must be accessed by this method only
struct proc *claim_proc(int i) {
  acquire(&proc_list.proc_lock);
  struct proc *p = (struct proc *)v_get(&proc_list.proc, i);

  if (p == 0) {
    release(&proc_list.proc_lock);
    return 0;
  }
  // Here sync is needed because stop_watching_proc accesses this field without
  // any lock
  __sync_fetch_and_add(&p->watching, 1);
  release(&proc_list.proc_lock);

  return p;
}

// When we are no longer using p, we can stop watching it
void stop_watching_proc(struct proc *p) {
  __sync_fetch_and_sub(&p->watching, 1);
}

int proc_list_size() { return proc_list.proc.size; }

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl) { k_pagetable = kpgtbl; }

// initialize the proc table.
void procinit(void) {
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  proc_list_init();
  init_pool();
  init_kstack_provider();
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid() {
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int allocpid() {
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *allocproc(void) {
  struct proc *p = malloc(sizeof(struct proc));
  if (p == 0) return 0;
  memset(p, 0, sizeof(struct proc));

  initlock(&p->lock, "p_lock");
  acquire(&p->lock);
  p->pid = allocpid();
  p->state = USED;
  p->list_index = -1;

  void *kstack_page = kalloc();
  if (kstack_page == 0) {
    freeproc(p);
    return 0;
  }
  uint64 kstack_va = get_kstack_va();
  if (mappages(k_pagetable, kstack_va, PGSIZE, (uint64)kstack_page,
               PTE_R | PTE_W) != 0) {
    kfree(kstack_page);
    freeproc(p);
    return 0;
  }
  p->kstack = kstack_va;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0) {
    freeproc(p);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0) {
    freeproc(p);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  // Put process in a process list
  p->list_index = push_proc(p);
  if (p->list_index < 0) {
    p->list_index = 0;
    freeproc(p);
    return 0;
  }
  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void freeproc(struct proc *p) {
  if (p->trapframe) kfree((void *)p->trapframe);
  if (p->pagetable) proc_freepagetable(p->pagetable, p->sz);
  if (p->kstack) {
    uvmunmap(k_pagetable, p->kstack, 1, 1);
    return_kstack_va(p->kstack);
  }
  remove_proc_from_list(p);
  release(&p->lock);

  // Our struct is now unused, so we want to free it when nobody is accessing it
  // anymore
  push_pool(p);
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t proc_pagetable(struct proc *p) {
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0) return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline,
               PTE_R | PTE_X) < 0) {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)(p->trapframe),
               PTE_R | PTE_W) < 0) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02, 0x97,
                    0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02, 0x93, 0x08,
                    0x70, 0x00, 0x73, 0x00, 0x00, 0x00, 0x93, 0x08, 0x20,
                    0x00, 0x73, 0x00, 0x00, 0x00, 0xef, 0xf0, 0x9f, 0xff,
                    0x2f, 0x69, 0x6e, 0x69, 0x74, 0x00, 0x00, 0x24, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void) {
  struct proc *p;

  p = allocproc();
  initproc = p;

  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n) {
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0) {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if (n < 0) {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void) {
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0) {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
    freeproc(np);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i]) np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p) {
  struct proc *pp;

  int proc_number = proc_list_size();

  for (int i = 0; i < proc_number; i++) {
    if ((pp = claim_proc(i)) == 0) continue;

    if (pp->parent == p) {
      pp->parent = initproc;
      wakeup(initproc);
    }
    stop_watching_proc(pp);
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status) {
  struct proc *p = myproc();

  if (p == initproc) panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd]) {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr) {
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;) {
    // Scan through table looking for exited children.
    havekids = 0;

    int proc_number = proc_list_size();

    for (int i = 0; i < proc_number; i++) {
      if ((pp = claim_proc(i)) == 0) continue;

      if (pp->parent == p) {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE) {
          // Found one.
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            stop_watching_proc(pp);
            return -1;
          }
          freeproc(pp);
          release(&wait_lock);
          stop_watching_proc(pp);
          return pid;
        }
        release(&pp->lock);
      }
      stop_watching_proc(pp);
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p)) {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock);  // DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void) {
  struct proc *p;
  struct cpu *c = mycpu();
  int sched_rounds = 0;

  c->proc = 0;
  for (;;) {
    // Maybe there are some processes waiting to be freed?
    if (++sched_rounds == 1000) {
      sched_rounds = 0;
      free_pool(1);
    }
    int proc_number = proc_list_size();

    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for (int i = 0; i < proc_number; i++) {
      if ((p = claim_proc(i)) == 0) continue;

      acquire(&p->lock);
      if (p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;

        // We need to flush kstack va from tlb because there can be wrong
        // mapping from and old process.
        // TODO: Add a flag that shows if this va has already been flushed
        //  on this hart. Bitmask, for example
        sfence_vma_va(p->kstack);

        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);

      stop_watching_proc(p);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void) {
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock)) panic("sched p->lock");
  if (mycpu()->noff != 1) panic("sched locks");
  if (p->state == RUNNING) panic("sched running");
  if (intr_get()) panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void) {
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void) {
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk) {
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  // DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan) {
  struct proc *p;

  int proc_number = proc_list_size();

  for (int i = 0; i < proc_number; i++) {
    if ((p = claim_proc(i)) == 0) continue;

    if (p != myproc()) {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
    stop_watching_proc(p);
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid) {
  struct proc *p;

  int proc_number = proc_list_size();

  for (int i = 0; i < proc_number; i++) {
    if ((p = claim_proc(i)) == 0) continue;

    acquire(&p->lock);
    if (p->pid == pid) {
      p->killed = 1;
      if (p->state == SLEEPING) {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      stop_watching_proc(p);
      return 0;
    }
    release(&p->lock);
    stop_watching_proc(p);
  }
  return -1;
}

void setkilled(struct proc *p) {
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int killed(struct proc *p) {
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len) {
  struct proc *p = myproc();
  if (user_dst) {
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len) {
  struct proc *p = myproc();
  if (user_src) {
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void) {
  static char *states[] = {
      [UNUSED] "unused",   [USED] "used",   [SLEEPING] "sleep ",
      [RUNNABLE] "runble", [RUNNING] "run", [ZOMBIE] "zombie"};
  struct proc *p;
  char *state;

  printf("\n");
  int proc_number = proc_list_size();
  print_pool();
  printf("Proc seek len is %d\n", proc_number);

  for (int i = 0; i < proc_number; i++) {
    p = (struct proc *)v_get(&proc_list.proc, i);

    if (p == 0) continue;

    if (p->state == UNUSED) continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("pid = %d; state = %s; name = %s; ind = %d", p->pid, state, p->name,
           p->list_index);
    printf("\n");
  }
}
