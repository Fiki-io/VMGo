#include "elf_loader.h"
#include "../include/vm_types.h"
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <vector>
#include <string>
#include <algorithm>

namespace vmgo {

static inline uintptr_t align_down(uintptr_t val, size_t alignment) {
    return val & ~(alignment - 1);
}

static inline uintptr_t align_up(uintptr_t val, size_t alignment) {
    return (val + alignment - 1) & ~(alignment - 1);
}

#if defined(__aarch64__)
__attribute__((naked, noreturn)) static void jump_to_entry(uintptr_t entry, uintptr_t sp) {
    __asm__ volatile (
        "mov sp, x1\n"
        "mov x1, #0\n"
        "mov x2, #0\n"
        "mov x3, #0\n"
        "mov x4, #0\n"
        "mov x5, #0\n"
        "mov x6, #0\n"
        "mov x7, #0\n"
        "mov x8, #0\n"
        "mov x9, #0\n"
        "mov x10, #0\n"
        "mov x11, #0\n"
        "mov x12, #0\n"
        "mov x13, #0\n"
        "mov x14, #0\n"
        "mov x15, #0\n"
        "mov x16, #0\n"
        "mov x17, #0\n"
        "mov x18, #0\n"
        "mov x19, #0\n"
        "mov x20, #0\n"
        "mov x21, #0\n"
        "mov x22, #0\n"
        "mov x23, #0\n"
        "mov x24, #0\n"
        "mov x25, #0\n"
        "mov x26, #0\n"
        "mov x27, #0\n"
        "mov x28, #0\n"
        "mov x29, #0\n"
        "mov x30, #0\n"
        "br x0\n"
    );
}
#elif defined(__arm__)
__attribute__((naked, noreturn)) static void jump_to_entry(uintptr_t entry, uintptr_t sp) {
    __asm__ volatile (
        "mov sp, r1\n"
        "mov r1, #0\n"
        "mov r2, #0\n"
        "mov r3, #0\n"
        "mov r4, #0\n"
        "mov r5, #0\n"
        "mov r6, #0\n"
        "mov r7, #0\n"
        "mov r8, #0\n"
        "mov r9, #0\n"
        "mov r10, #0\n"
        "mov r11, #0\n"
        "mov r12, #0\n"
        "mov lr, #0\n"
        "bx r0\n"
    );
}
#elif defined(__x86_64__)
__attribute__((naked, noreturn)) static void jump_to_entry(uintptr_t entry, uintptr_t sp) {
    __asm__ volatile (
        "mov %rsi, %rsp\n"
        "xor %rax, %rax\n"
        "xor %rbx, %rbx\n"
        "xor %rcx, %rcx\n"
        "xor %rdx, %rdx\n"
        "xor %rsi, %rsi\n"
        "xor %r8, %r8\n"
        "xor %r9, %r9\n"
        "xor %r10, %r10\n"
        "xor %r11, %r11\n"
        "xor %r12, %r12\n"
        "xor %r13, %r13\n"
        "xor %r14, %r14\n"
        "xor %r15, %r15\n"
        "xor %rbp, %rbp\n"
        "jmp *%rdi\n"
    );
}
#else
static void jump_to_entry(uintptr_t /* entry */, uintptr_t /* sp */) {
    _exit(0);
}
#endif

bool ElfLoader::execute(
    const std::string& binaryPath,
    const std::vector<std::string>& args,
    const std::vector<std::string>& envVars,
    const std::string& sandboxRoot
) {
    LOGI("ElfLoader: Loading binary in user-space: %s", binaryPath.c_str());

    int binFd = open(binaryPath.c_str(), O_RDONLY);
    if (binFd < 0) {
        LOGE("ElfLoader: Failed to open binary %s: %s", binaryPath.c_str(), strerror(errno));
        return false;
    }

    // Read 64-bit ELF Header
    Elf64_Ehdr ehdr{};
    if (pread(binFd, &ehdr, sizeof(ehdr), 0) != sizeof(ehdr)) {
        LOGE("ElfLoader: Failed to read ELF header from %s", binaryPath.c_str());
        close(binFd);
        return false;
    }

    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        LOGE("ElfLoader: Invalid ELF magic in %s", binaryPath.c_str());
        close(binFd);
        return false;
    }

    size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    if (pageSize == 0) pageSize = 4096;

    // Read all Program Headers
    std::vector<Elf64_Phdr> phdrs(ehdr.e_phnum);
    if (pread(binFd, phdrs.data(), sizeof(Elf64_Phdr) * ehdr.e_phnum, ehdr.e_phoff) !=
        static_cast<ssize_t>(sizeof(Elf64_Phdr) * ehdr.e_phnum)) {
        LOGE("ElfLoader: Failed to read program headers");
        close(binFd);
        return false;
    }

    // Check for dynamic interpreter (PT_INTERP)
    std::string interpPath;
    for (const auto& phdr : phdrs) {
        if (phdr.p_type == PT_INTERP) {
            std::vector<char> interpBuf(phdr.p_filesz + 1, 0);
            pread(binFd, interpBuf.data(), phdr.p_filesz, phdr.p_offset);
            interpPath = interpBuf.data();
            break;
        }
    }

    // Calculate binary memory span across PT_LOAD segments
    uintptr_t minVaddr = UINTPTR_MAX;
    uintptr_t maxVaddr = 0;
    for (const auto& phdr : phdrs) {
        if (phdr.p_type == PT_LOAD) {
            minVaddr = std::min(minVaddr, static_cast<uintptr_t>(phdr.p_vaddr));
            maxVaddr = std::max(maxVaddr, static_cast<uintptr_t>(phdr.p_vaddr + phdr.p_memsz));
        }
    }

    uintptr_t loadBase = 0;
    if (ehdr.e_type == ET_DYN) {
        // PIE binary: map somewhere in memory
        size_t totalSpan = align_up(maxVaddr - minVaddr, pageSize);
        void* reserved = mmap(nullptr, totalSpan + pageSize * 4, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (reserved == MAP_FAILED) {
            LOGE("ElfLoader: Failed to reserve memory span: %s", strerror(errno));
            close(binFd);
            return false;
        }
        loadBase = reinterpret_cast<uintptr_t>(reserved);
    }

    // Map each PT_LOAD segment of the binary
    uintptr_t phdrLoadedAddr = 0;
    for (const auto& phdr : phdrs) {
        if (phdr.p_type == PT_LOAD) {
            uintptr_t segStart = loadBase + phdr.p_vaddr;
            uintptr_t pageStart = align_down(segStart, pageSize);
            size_t pageOffset = segStart - pageStart;
            size_t mapSize = align_up(phdr.p_memsz + pageOffset, pageSize);

            void* mem = mmap(reinterpret_cast<void*>(pageStart), mapSize,
                             PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
            if (mem == MAP_FAILED) {
                LOGE("ElfLoader: Failed to map segment at 0x%lx: %s", (unsigned long)pageStart, strerror(errno));
                close(binFd);
                return false;
            }

            if (phdr.p_filesz > 0) {
                pread(binFd, reinterpret_cast<void*>(segStart), phdr.p_filesz, phdr.p_offset);
            }

            // Zero out remaining BSS
            if (phdr.p_memsz > phdr.p_filesz) {
                memset(reinterpret_cast<void*>(segStart + phdr.p_filesz), 0, phdr.p_memsz - phdr.p_filesz);
            }

            int prot = 0;
            if (phdr.p_flags & PF_R) prot |= PROT_READ;
            if (phdr.p_flags & PF_W) prot |= PROT_WRITE;
            if (phdr.p_flags & PF_X) prot |= PROT_EXEC;
            mprotect(reinterpret_cast<void*>(pageStart), mapSize, prot);
        } else if (phdr.p_type == PT_PHDR) {
            phdrLoadedAddr = loadBase + phdr.p_vaddr;
        }
    }
    close(binFd);

    if (phdrLoadedAddr == 0) {
        phdrLoadedAddr = loadBase + ehdr.e_phoff;
    }

    uintptr_t interpBase = 0;
    uintptr_t entryPoint = loadBase + ehdr.e_entry;

    // Load interpreter if needed
    if (!interpPath.empty()) {
        LOGI("ElfLoader: Dynamic binary requires interpreter: %s", interpPath.c_str());

        // Resolve interpreter in sandbox
        std::vector<std::string> interpCandidates = {
            sandboxRoot + interpPath,
            sandboxRoot + "/system/bin/linker64",
            sandboxRoot + "/system/bin/linker",
            interpPath
        };

        std::string resolvedInterp;
        for (const auto& cand : interpCandidates) {
            if (access(cand.c_str(), R_OK) == 0) {
                resolvedInterp = cand;
                break;
            }
        }

        if (resolvedInterp.empty()) {
            LOGE("ElfLoader: Could not find interpreter in sandbox");
            return false;
        }

        LOGI("ElfLoader: Loading interpreter from: %s", resolvedInterp.c_str());
        int interpFd = open(resolvedInterp.c_str(), O_RDONLY);
        if (interpFd < 0) {
            LOGE("ElfLoader: Failed to open interpreter: %s", strerror(errno));
            return false;
        }

        Elf64_Ehdr interpEhdr{};
        pread(interpFd, &interpEhdr, sizeof(interpEhdr), 0);

        std::vector<Elf64_Phdr> interpPhdrs(interpEhdr.e_phnum);
        pread(interpFd, interpPhdrs.data(), sizeof(Elf64_Phdr) * interpEhdr.e_phnum, interpEhdr.e_phoff);

        uintptr_t interpMinVaddr = UINTPTR_MAX;
        uintptr_t interpMaxVaddr = 0;
        for (const auto& ph : interpPhdrs) {
            if (ph.p_type == PT_LOAD) {
                interpMinVaddr = std::min(interpMinVaddr, static_cast<uintptr_t>(ph.p_vaddr));
                interpMaxVaddr = std::max(interpMaxVaddr, static_cast<uintptr_t>(ph.p_vaddr + ph.p_memsz));
            }
        }

        size_t interpTotal = align_up(interpMaxVaddr - interpMinVaddr, pageSize);
        void* interpReserved = mmap(nullptr, interpTotal + pageSize * 4, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        interpBase = reinterpret_cast<uintptr_t>(interpReserved);

        for (const auto& ph : interpPhdrs) {
            if (ph.p_type == PT_LOAD) {
                uintptr_t segStart = interpBase + ph.p_vaddr;
                uintptr_t pageStart = align_down(segStart, pageSize);
                size_t pageOffset = segStart - pageStart;
                size_t mapSize = align_up(ph.p_memsz + pageOffset, pageSize);

                mmap(reinterpret_cast<void*>(pageStart), mapSize,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

                if (ph.p_filesz > 0) {
                    pread(interpFd, reinterpret_cast<void*>(segStart), ph.p_filesz, ph.p_offset);
                }

                if (ph.p_memsz > ph.p_filesz) {
                    memset(reinterpret_cast<void*>(segStart + ph.p_filesz), 0, ph.p_memsz - ph.p_filesz);
                }

                int prot = 0;
                if (ph.p_flags & PF_R) prot |= PROT_READ;
                if (ph.p_flags & PF_W) prot |= PROT_WRITE;
                if (ph.p_flags & PF_X) prot |= PROT_EXEC;
                mprotect(reinterpret_cast<void*>(pageStart), mapSize, prot);
            }
        }
        close(interpFd);

        entryPoint = interpBase + interpEhdr.e_entry;
        LOGI("ElfLoader: Interpreter mapped at 0x%lx, entry=0x%lx", (unsigned long)interpBase, (unsigned long)entryPoint);
    }

    // Allocate an 8MB stack
    size_t stackSize = 8 * 1024 * 1024;
    void* stackMem = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stackMem == MAP_FAILED) {
        LOGE("ElfLoader: Failed to allocate stack: %s", strerror(errno));
        return false;
    }

    uint8_t* stackTop = reinterpret_cast<uint8_t*>(stackMem) + stackSize;

    // Helper: push string to stack and return its address
    auto pushString = [&stackTop](const std::string& str) -> uintptr_t {
        size_t len = str.size() + 1;
        stackTop -= len;
        memcpy(stackTop, str.c_str(), len);
        return reinterpret_cast<uintptr_t>(stackTop);
    };

    // Helper: push raw bytes
    auto pushBytes = [&stackTop](const void* src, size_t size) -> uintptr_t {
        stackTop -= size;
        memcpy(stackTop, src, size);
        return reinterpret_cast<uintptr_t>(stackTop);
    };

    // 1. Push string data to top of stack
    std::string platform = "aarch64";
    uintptr_t platformAddr = pushString(platform);

    uint8_t randomBytes[16] = { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
                                0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
    uintptr_t randomAddr = pushBytes(randomBytes, sizeof(randomBytes));

    uintptr_t execfnAddr = pushString(binaryPath);

    std::vector<uintptr_t> envAddrs;
    for (const auto& ev : envVars) {
        envAddrs.push_back(pushString(ev));
    }

    std::vector<uintptr_t> argAddrs;
    for (const auto& arg : args) {
        argAddrs.push_back(pushString(arg));
    }

    // Align stack pointer to 16 bytes
    stackTop = reinterpret_cast<uint8_t*>(align_down(reinterpret_cast<uintptr_t>(stackTop), 16));

    // Helper: push 64-bit value
    auto push64 = [&stackTop](uint64_t val) {
        stackTop -= sizeof(uint64_t);
        *reinterpret_cast<uint64_t*>(stackTop) = val;
    };

    // 2. Push Auxiliary Vector (AT_*)
    push64(0); push64(AT_NULL);
    push64(platformAddr); push64(AT_PLATFORM);
    push64(execfnAddr); push64(AT_EXECFN);
    push64(randomAddr); push64(AT_RANDOM);
    push64(0); push64(AT_SECURE);
    push64(getegid()); push64(AT_EGID);
    push64(getgid()); push64(AT_GID);
    push64(geteuid()); push64(AT_EUID);
    push64(getuid()); push64(AT_UID);
    push64(loadBase + ehdr.e_entry); push64(AT_ENTRY);
    push64(0); push64(AT_FLAGS);
    push64(interpBase); push64(AT_BASE);
    push64(pageSize); push64(AT_PAGESZ);
    push64(ehdr.e_phnum); push64(AT_PHNUM);
    push64(sizeof(Elf64_Phdr)); push64(AT_PHENT);
    push64(phdrLoadedAddr); push64(AT_PHDR);

    // 3. Push envp pointers (NULL terminated)
    push64(0);
    for (auto it = envAddrs.rbegin(); it != envAddrs.rend(); ++it) {
        push64(*it);
    }

    // 4. Push argv pointers (NULL terminated)
    push64(0);
    for (auto it = argAddrs.rbegin(); it != argAddrs.rend(); ++it) {
        push64(*it);
    }

    // 5. Push argc
    push64(argAddrs.size());

    uintptr_t sp = reinterpret_cast<uintptr_t>(stackTop);

    LOGI("ElfLoader: Stack prepared at 0x%lx. Jumping to entry: 0x%lx...", (unsigned long)sp, (unsigned long)entryPoint);

    // JUMP! This transfers execution to the loaded ELF / linker64
    jump_to_entry(entryPoint, sp);

    return true;
}

} // namespace vmgo
