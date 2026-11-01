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
void loader_cleanup()
{
  unhook();
  for (size_t i = 0; i < nmapped; i++){
    munmap((void *)mapped[i], PG);
  }
  free(mapped);
  free(ph);
  mapped = NULL;
  ph = NULL;
  nmapped = maxpages = 0;
  if (fd >= 0){
    close(fd);
  }
  fd = -1;
}

static void die(const char *msg)
{
  if (msg){
    fprintf(stderr, "SimpleSmartLoader: %s\n", msg);
  }
  else{
    perror("SimpleSmartLoader: open");
  }
  loader_cleanup();
  exit(1);
}

static void r_header(const char *path)
{
  struct stat st;
  if ((fd = open(path, O_RDONLY)) < 0){
    die(NULL);
  }
  if (fstat(fd, &st) < 0){
    die("fstat failed");
  }
  if (!S_ISREG(st.st_mode)){
    die("not a regular file");
  }
  fsize = st.st_size;
  ssize_t n = pread(fd, &eh, sizeof eh, 0);
  if (n < 0){
    die("cannot read file");
  }
  if (n < SELFMAG || memcmp(eh.e_ident, ELFMAG, SELFMAG)){
    die("not an ELF file");
  }
  if ((size_t)n < sizeof eh){
    die("ELF header too short");
  }
  if (eh.e_ident[EI_CLASS] != ELFCLASS32){
    die("not a 32-bit ELF file");
  }
  if (eh.e_ident[EI_DATA] != ELFDATA2LSB){
    die("not a little-endian ELF file");
  }
  if (eh.e_ident[EI_VERSION] != EV_CURRENT){
    die("bad ELF identification version");
  }
  if (eh.e_version != EV_CURRENT || eh.e_ehsize != sizeof eh){
    die("bad ELF header");
  }
  if (eh.e_machine != EM_386){
    die("not an x86 ELF file");
  }
  if (eh.e_type != ET_EXEC){
    die("not an ET_EXEC executable (PIE is not supported)");
  }
  if (!eh.e_phnum || eh.e_phentsize != sizeof(Elf32_Phdr)){
    die("bad program header table");
  }
  size_t phsz = (size_t)eh.e_phnum * sizeof(Elf32_Phdr);
  if ((uint64_t)eh.e_phoff + phsz > (uint64_t)fsize){
    die("program headers out of bounds");
  }
  if (!(ph = malloc(phsz))){
    die("out of memory");
  }
  if (read_full(eh.e_phoff, ph, phsz) < 0){
    die("cannot read program headers");
  }
}

static void check_seg()
{
  size_t pages = 0;
  int entry_ok = 0;
  for (int i = 0; i < eh.e_phnum; i++)
  {
    Elf32_Phdr *p = &ph[i];
    if (!is_load(p)){
      continue;
    }
    if (p->p_filesz > p->p_memsz){
      die("bad segment: filesz > memsz");
    }
    if (p->p_align > 1 && (p->p_align & (p->p_align - 1)) != 0){
      die("invalid segment alignment");
    }
    if ((uint64_t)p->p_vaddr + p->p_memsz > 0xFFFFFFFFull){
      die("bad segment: address overflow");
    }
    if ((uint64_t)p->p_offset + p->p_filesz > (uint64_t)fsize){
      die("bad segment: beyond end of file");
    }
    for (int j = 0; j < i; j++){
      if (is_load(&ph[j]) && p->p_vaddr < (uint64_t)ph[j].p_vaddr + ph[j].p_memsz && ph[j].p_vaddr < (uint64_t)p->p_vaddr + p->p_memsz){
        die("bad ELF: overlapping segments");
      }
    }
    pages += ((p->p_vaddr + p->p_memsz - 1) >> 12) - (p->p_vaddr >> 12) + 1;
    if ((p->p_flags & PF_X) && eh.e_entry >= p->p_vaddr && eh.e_entry < (uint64_t)p->p_vaddr + p->p_memsz){
      entry_ok = 1;
    }
  }
  if (!pages){
    die("no loadable segments");
  }
  if (!entry_ok){
    die("entrypoint not in an executable segment");
  }
  if (!(mapped = calloc(pages, sizeof *mapped))){
    die("out of memory");
  }
  maxpages = pages;
}

static void print_stats()
{
  printf("Total Page Faults: %lu\nTotal Page Allocations: %lu\nTotal Internal Fragmentation: %.2f KB\n", faults, allocs, frag / 1024.0);
}

void load_and_run_elf(char **exe)
{
  if (!exe || !exe[0]){
    die("no executable given");
  }
  faults = allocs = frag = 0;
  r_header(exe[0]);
  check_seg();
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = on_segv;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  stack_t ss = {.ss_sp = altstk, .ss_size = sizeof altstk, .ss_flags = 0};
  if (sigaction(SIGSEGV, &sa, &old_sa) < 0){
    die("sigaction failed");
  }
  if (sigaltstack(&ss, &old_ss) < 0)
  {
    sigaction(SIGSEGV, &old_sa, NULL);
    die("sigaltstack failed");
  }
  hooked = 1;
  if (sigsetjmp(jb, 1) == 0)
  {
    int (*start)(void) = (int (*)(void))(uintptr_t)eh.e_entry;
    int res = start();
    unhook();
    printf("User _start return value = %d\n", res);
    print_stats();
    return;
  }
  unhook();
  fprintf(stderr, "SimpleSmartLoader: %s at 0x%08lx\n", fmsg, (unsigned long)faddr);
  print_stats();
  loader_cleanup();
  exit(1);
}