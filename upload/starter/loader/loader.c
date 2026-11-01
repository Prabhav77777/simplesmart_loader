#include "loader.h"

Elf32_Ehdr *ehdr;
Elf32_Phdr *phdr;
int fd=-1;
void *test_mem=NULL;
void *seg_mem=NULL;
size_t seg_size=0;
/*
 * release memory and other cleanups
 */
void loader_cleanup()
{
    if(fd>=0){
        
        close(fd);
        fd=-1;
    }
    if(test_mem!=NULL){
        free(test_mem);
        test_mem=NULL;
    }
    if(seg_mem!=NULL){
        munmap(seg_mem, seg_size);
        seg_mem=NULL;
    }
}
void load_and_run_elf(char *exe)
{
    fd = open(exe, O_RDONLY);
    off_t file_size = lseek(fd, 0, SEEK_END);
    if(file_size==-1){
        //error 2
        perror("error in file size");
        close(fd);
        exit(1);
    }
    if(lseek(fd, 0, SEEK_SET)==-1){
        perror("error in lseek");
        close(fd);
        exit(1);
    }
    test_mem = malloc(file_size);
    if (test_mem == NULL)
    {
        //error 3
        perror("error malloc");
        exit(1);
    }

    ssize_t bytes_read = read(fd, test_mem, file_size);
    if(bytes_read==-1){
        //error 4
        perror("error reading elf file");
        free(test_mem);
        close(fd);
        exit(1);
    }

    if (bytes_read != file_size)
    {
        fprintf(stderr, "Error reading ELF file");
        free(test_mem);
        close(fd);
        exit(1);
    }
    ehdr = (Elf32_Ehdr *)test_mem;

    phdr = (Elf32_Phdr *)((char *)test_mem + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; i++)
    {
        if (phdr[i].p_type == PT_LOAD)
        {
            if (ehdr->e_entry >= phdr[i].p_vaddr && ehdr->e_entry < phdr[i].p_vaddr + phdr[i].p_memsz)
            {
                uintptr_t page_size = 4096;
                uintptr_t page_start = phdr[i].p_vaddr & ~(page_size - 1);
                uintptr_t page_offset = phdr[i].p_vaddr & (page_size - 1);
                size_t map_size = phdr[i].p_memsz + page_offset;
                void *seg = mmap(
                    (void *)page_start,
                    map_size,
                    PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                    -1,
                    0);
                if (seg == MAP_FAILED)
                {
                    // error8
                    perror("mmap failed");
                    exit(1);
                }
                seg_mem = seg;
                seg_size = map_size;
                memcpy(
                    (char *)seg + page_offset,
                    (char *)test_mem + phdr[i].p_offset,
                    phdr[i].p_filesz);
                if (phdr[i].p_memsz > phdr[i].p_filesz)
                {
                    memset(
                        (char *)seg + page_offset + phdr[i].p_filesz,
                        0,
                        phdr[i].p_memsz - phdr[i].p_filesz);
                }
                uintptr_t entry_offset = ehdr->e_entry - phdr[i].p_vaddr;
                void *entry_address = (char *)seg + page_offset + entry_offset;
                typedef int (*start_func_t)(void);
                start_func_t start = (start_func_t)entry_address;
                int result = start();
                printf("User _start return value = %d\n", result);
            }
        }
    }
}