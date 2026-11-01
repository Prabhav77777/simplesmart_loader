#include "loader.h"

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    printf("Usage: %s <ELF Executable> \n", argv[0]);
    exit(1);
  }
  int fd = open(argv[1], O_RDONLY);
  if (fd < 0)
  {
    // error 1
    perror("Error opening executable");
    exit(1);
  }
  Elf32_Ehdr header;
  ssize_t n = read(fd, &header, sizeof(Elf32_Ehdr));
  close(fd);
  if (n != sizeof(Elf32_Ehdr))
  {
    // error 5
    fprintf(stderr, "Error: Could not read ELF header.\n");
    exit(1);
  }
  if (header.e_ident[EI_MAG0] != ELFMAG0 || header.e_ident[EI_MAG1] != ELFMAG1 || header.e_ident[EI_MAG2] != ELFMAG2 || header.e_ident[EI_MAG3] != ELFMAG3)
  {
    // error 6
    fprintf(stderr, "Error: File is not an ELF file.\n");
    exit(1);
  }
  if (header.e_ident[EI_CLASS] != ELFCLASS32)
  {
    // error 7
    fprintf(stderr, "Error: Only 32-bit ELF files are supported.\n");
    exit(1);
  }
  load_and_run_elf(argv[1]);
  // 3. invoke the cleanup routine inside the loader
  loader_cleanup();
  return 0;
}