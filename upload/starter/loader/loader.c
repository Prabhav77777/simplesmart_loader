#include "loader.h"
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#define PG 4096u
#define MASK (~(PG - 1u))

static int fd = -1, hooked;
static off_t fsize;
static Elf32_Ehdr eh;
static Elf32_Phdr *ph;
static uintptr_t *mapped;
static size_t nmapped, maxpages;
static unsigned long faults, allocs, frag;
static struct sigaction old_sa;
static stack_t old_ss;
static char altstk[64 * 1024];
static sigjmp_buf jb;
static const char *fmsg;
static uintptr_t faddr;

static int is_load(const Elf32_Phdr *p){
  return p->p_type == PT_LOAD && p->p_memsz > 0;
}

static int read_full(off_t off, void *buf, size_t len)
{
  for(size_t got=0; got < len;)
  {
    ssize_t r=pread(fd, (char *)buf + got, len - got, off + (off_t)got);
    if (r < 0 && errno == EINTR){
      continue;
    }
    if (r <= 0){
      return -1;
    }
    got += (size_t)r;
  }
  return 0;
}

static int page_in_seg(uintptr_t pg)
{
  for (int i = 0; i < eh.e_phnum; i++){
    if (is_load(&ph[i]) && (uint64_t)ph[i].p_vaddr + ph[i].p_memsz > pg && ph[i].p_vaddr < (uint64_t)pg + PG){
      return 1;
    }
  }
  return 0;
}

static int fill_page(uintptr_t pg, uint64_t *used)
{
  int prot = 0;
  uint64_t pend = (uint64_t)pg + PG;
  *used = 0;
  for (int i = 0; i < eh.e_phnum; i++)
  {
    Elf32_Phdr *p = &ph[i];
    uint64_t lo = p->p_vaddr, hi = lo + p->p_memsz, fend = lo + p->p_filesz;
    if (!is_load(p) || hi <= pg || lo >= pend){
      continue;
    }
    uint64_t from = pg > lo ? pg : lo, to = pend < fend ? pend : fend;
    if (to > from && read_full((off_t)p->p_offset + (off_t)(from - lo), (void *)(uintptr_t)from, (size_t)(to - from)) < 0){
      return -1;
    }
    *used += (pend < hi ? pend : hi) - from;
    prot |= (p->p_flags & PF_R ? PROT_READ : 0) | (p->p_flags & PF_W ? PROT_WRITE : 0) | (p->p_flags & PF_X ? PROT_EXEC : 0);
  }
  return prot;
}

static void fatal_fault(const char *msg, uintptr_t addr)
{
  fmsg = msg;
  faddr = addr;
  siglongjmp(jb, 1);
}

static void on_segv(int sig, siginfo_t *si, void *ctx)
{
  (void)sig;
  (void)ctx;
  uintptr_t addr = (uintptr_t)si->si_addr, pg = addr & MASK;
  uint64_t used;
  if (!page_in_seg(pg)){
    fatal_fault("invalid memory access", addr);
  }
  if (si->si_code != SEGV_MAPERR || nmapped >= maxpages){
    fatal_fault("memory protection violation", addr);
  }
  faults++;
  void *m = mmap((void *)pg, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (m == MAP_FAILED){
    fatal_fault("mmap failed", pg);
  }
  if ((uintptr_t)m != pg)
  {
    munmap(m, PG);
    fatal_fault("mmap failed", pg);
  }
  mapped[nmapped++] = pg;
  allocs++;
  int prot = fill_page(pg, &used);
  if (prot < 0){
    fatal_fault("cannot read executable", pg);
  }
  frag += (unsigned long)(PG - used);
  if (mprotect((void *)pg, PG, prot) < 0){
    fatal_fault("mprotect failed", pg);
  }
}

static void unhook()
{
  if (hooked)
  {
    sigaction(SIGSEGV, &old_sa, NULL);
    sigaltstack(&old_ss, NULL);
  }
  hooked = 0;
}