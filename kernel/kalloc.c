// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define STOLEN_NUM 8

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  for(int i = 0; i < NCPU; i++){
    initlock(&kmem[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}


void
freerange(void *pa_start, void *pa_end)
{
  char *p;

  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}


// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 ||
     (char*)pa < end ||
     (uint64)pa >= PHYSTOP)
    panic("kfree");

  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;

  push_off();
  int id = cpuid();

  acquire(&kmem[id].lock);

  r->next = kmem[id].freelist;
  kmem[id].freelist = r;

  release(&kmem[id].lock);

  pop_off();
}




// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r = 0;

  push_off();
  int id = cpuid();

  // 1. First try the local freelist.
  acquire(&kmem[id].lock);

  r = kmem[id].freelist;
  if(r){
    kmem[id].freelist = r->next;
    release(&kmem[id].lock);
  } else {
    release(&kmem[id].lock);

    struct run *stolen_head = 0;
    struct run *stolen_end = 0;

    // 2. Local list empty: steal up to 8 pages.
    for(int n = 1; n < NCPU; n++){
      int j = (id + NCPU - n) % NCPU;

      acquire(&kmem[j].lock);

      if(kmem[j].freelist == 0){
        release(&kmem[j].lock);
        continue;
      }

      stolen_head = kmem[j].freelist;

      for(int cnt = 0;
          kmem[j].freelist != 0 && cnt < STOLEN_NUM;
          cnt++){

        stolen_end = kmem[j].freelist;
        kmem[j].freelist = stolen_end->next;
      }

      stolen_end->next = 0;

      release(&kmem[j].lock);
      break;
    }

    // 3. Put the batch on our local list and consume one page
    //    while holding the local lock only once.
    if(stolen_head){
      acquire(&kmem[id].lock);

      stolen_end->next = kmem[id].freelist;
      kmem[id].freelist = stolen_head;

      r = kmem[id].freelist;
      kmem[id].freelist = r->next;

      release(&kmem[id].lock);
    }
  }

  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE);

  return (void*)r;
}
