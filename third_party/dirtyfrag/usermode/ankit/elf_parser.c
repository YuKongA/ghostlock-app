#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/elf.h>
#include "reporter.h"

int find_hook_target(const char *libcxx, const char *symname,
                     uint64_t *hook_target, uint64_t *payload_target,
                     uint32_t *first_instruction, struct Reporter *reporter) {
    *hook_target = 0;
    *payload_target = 0;

    int fd = open(libcxx, O_RDONLY);
    if (fd < 0) {
        REPORTLN("open %s failed: %s", libcxx, strerror(errno));
        return 1;
    }

    Elf64_Ehdr hdr;
    if (read(fd, (char *)&hdr, sizeof(hdr)) < (ssize_t)sizeof(hdr)) {
        REPORTLN("read ELF header from %s failed: %s", libcxx, strerror(errno));
        close(fd); return 1;
    }
    if (strncmp((char *)hdr.e_ident, "\x7f""ELF", 4) != 0) {
        REPORTLN("invalid ELF: %s", libcxx);
        close(fd); return 1;
    }
    if (lseek64(fd, hdr.e_phoff, SEEK_SET) < 0) {
        REPORTLN("lseek64 e_phoff failed: %s", strerror(errno));
        close(fd); return 1;
    }
    if (hdr.e_phentsize != sizeof(Elf64_Phdr)) {
        REPORTLN("invalid phentsize: %d", hdr.e_phentsize);
        close(fd); return 1;
    }

    uint64_t executable_off = 0;
    uint64_t executable_vaddr = 0;
    for (int i = 0; i < hdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        if (read(fd, (char *)&phdr, sizeof(phdr)) < 0) {
            REPORTLN("read phdr[%d] failed: %s", i, strerror(errno));
            close(fd); return 1;
        }
        if (phdr.p_type == PT_LOAD && (phdr.p_flags & PF_X)) {
            *payload_target = phdr.p_offset + phdr.p_filesz;
            executable_off   = phdr.p_offset;
            executable_vaddr = phdr.p_vaddr;
            break;
        }
    }

    if (lseek64(fd, hdr.e_shoff + hdr.e_shstrndx * sizeof(Elf64_Shdr), SEEK_SET) < 0) {
        REPORTLN("lseek64 shstrndx failed: %s", strerror(errno));
        close(fd); return 1;
    }
    Elf64_Shdr str_shdr;
    if (read(fd, (char *)&str_shdr, sizeof(str_shdr)) < 0) {
        REPORTLN("read shstrndx failed: %s", strerror(errno));
        close(fd); return 1;
    }

    uint64_t dynstr = 0, dynsym_offset = 0, dynsym_size = 0;
    for (int i = 0; i < hdr.e_shnum; i++) {
        Elf64_Shdr shdr;
        if (lseek64(fd, hdr.e_shoff + i * sizeof(Elf64_Shdr), SEEK_SET) < 0) {
            REPORTLN("lseek64 shdr[%d] failed: %s", i, strerror(errno));
            close(fd); return 1;
        }
        if (read(fd, (char *)&shdr, sizeof(shdr)) < 0) {
            REPORTLN("read shdr[%d] failed: %s", i, strerror(errno));
            close(fd); return 1;
        }
        if (lseek64(fd, shdr.sh_name + str_shdr.sh_offset, SEEK_SET) < 0) {
            REPORTLN("lseek64 sh_name[%d] failed: %s", i, strerror(errno));
            close(fd); return 1;
        }
        char name[100];
        if (read(fd, name, sizeof(name) - 1) < 0) {
            REPORTLN("read sh_name[%d] failed: %s", i, strerror(errno));
            close(fd); return 1;
        }
        name[sizeof(name) - 1] = 0;
        if (strcmp(name, ".dynstr") == 0) dynstr = shdr.sh_offset;
        if (strcmp(name, ".dynsym") == 0) { dynsym_offset = shdr.sh_offset; dynsym_size = shdr.sh_size; }
    }

    if (dynstr == 0 || dynsym_offset == 0) {
        REPORTLN(".dynstr or .dynsym not found in %s", libcxx);
        close(fd); return 1;
    }

    for (int i = 0; i < (int)(dynsym_size / sizeof(Elf64_Sym)); i++) {
        Elf64_Sym sym;
        if (lseek64(fd, dynsym_offset + i * sizeof(Elf64_Sym), SEEK_SET) < 0) {
            REPORTLN("lseek64 dynsym[%d] failed: %s", i, strerror(errno));
            close(fd); return 1;
        }
        if (read(fd, (char *)&sym, sizeof(sym)) < 0) {
            REPORTLN("read dynsym[%d] failed: %s", i, strerror(errno));
            close(fd); return 1;
        }
        if (lseek64(fd, dynstr + sym.st_name, SEEK_SET) < 0) {
            REPORTLN("lseek64 st_name[%d] failed: %s", i, strerror(errno));
            close(fd); return 1;
        }
        char name[200];
        if (read(fd, name, sizeof(name) - 1) < 0) {
            REPORTLN("read st_name[%d] failed: %s", i, strerror(errno));
            close(fd); return 1;
        }
        name[sizeof(name) - 1] = 0;
        if (strcmp(name, symname) == 0)
            *hook_target = sym.st_value - executable_vaddr + executable_off;
    }

    if (*hook_target == 0) {
        REPORTLN("symbol %s not found in %s", symname, libcxx);
        close(fd); return 1;
    }

    if (lseek64(fd, *hook_target, SEEK_SET) < 0) {
        REPORTLN("lseek64 hook_target failed: %s", strerror(errno));
        close(fd); return 1;
    }
    if (read(fd, first_instruction, sizeof(*first_instruction)) < 0) {
        REPORTLN("read first instruction failed: %s", strerror(errno));
        close(fd); return 1;
    }

    if (*first_instruction == 0xd503233fU || *first_instruction == 0xd503245f) {
        REPORTLN("PACIASP/BTI at hook site, advancing +4");
        *hook_target += 4UL;
        if (read(fd, first_instruction, sizeof(*first_instruction)) < 0) {
            REPORTLN("read first instruction +4 failed: %s", strerror(errno));
            close(fd); return 1;
        }
    }

    close(fd);
    return 0;
}
