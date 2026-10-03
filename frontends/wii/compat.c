/* Nomes que a std do Linux referencia e o newlib do devkitPPC não exporta assim.
   O RetroArch liga este .a com -mrvl; símbolo faltando quebra o DOL, não o jogo.
   Cada função devolve a falha que a std já sabe tratar, ou encaminha para o nome
   que o newlib realmente tem. */
#define _GNU_SOURCE
#include <errno.h>
#include <malloc.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

extern int *__errno(void);

int *__errno_location(void) { return __errno(); }

int statx(int dirfd, const char *pathname, int flags, unsigned int mask, void *statxbuf) {
    (void)dirfd;
    (void)pathname;
    (void)flags;
    (void)mask;
    (void)statxbuf;
    errno = ENOSYS;
    return -1;
}

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)buf;
    (void)buflen;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

ssize_t copy_file_range(int fd_in, off_t *off_in, int fd_out, off_t *off_out, size_t len,
                        unsigned int flags) {
    (void)fd_in;
    (void)off_in;
    (void)fd_out;
    (void)off_out;
    (void)len;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    void *p;
    if (!memptr || alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return EINVAL;
    }
    p = memalign(alignment, size);
    if (!p) {
        return ENOMEM;
    }
    *memptr = p;
    return 0;
}

char *secure_getenv(const char *name) { return getenv(name); }

int dl_iterate_phdr(int (*callback)(void *info, size_t size, void *data), void *data) {
    (void)callback;
    (void)data;
    return 0;
}

int __register_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void), void *dso) {
    (void)prepare;
    (void)parent;
    (void)child;
    (void)dso;
    return 0;
}

int __cxa_thread_atexit_impl(void (*func)(void *), void *obj, void *dso) {
    (void)func;
    (void)obj;
    (void)dso;
    return 0;
}

/* O 750 não tem atômico de 64 bits. O LLVM emite estas chamadas, e o libgcc
   do devkitPPC não as traz. O Broadway é um núcleo: com a interrupção externa
   desligada, ler e escrever os oito bytes não rasga. */
static unsigned int irq_off(void) {
    unsigned int msr;
    unsigned int cleared;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    cleared = msr & ~0x8000u;
    __asm__ volatile("mtmsr %0; isync" : : "r"(cleared) : "memory");
    return msr;
}

static void irq_on(unsigned int msr) { __asm__ volatile("mtmsr %0" : : "r"(msr) : "memory"); }

static unsigned long long ler8(const void *ptr) {
    unsigned long long valor;
    memcpy(&valor, ptr, sizeof(valor));
    return valor;
}

static void gravar8(void *ptr, unsigned long long valor) { memcpy(ptr, &valor, sizeof(valor)); }

unsigned long long __atomic_load_8(const volatile void *ptr, int model) {
    unsigned int msr;
    unsigned long long valor;
    (void)model;
    msr = irq_off();
    valor = ler8((const void *)ptr);
    irq_on(msr);
    return valor;
}

void __atomic_store_8(volatile void *ptr, unsigned long long val, int model) {
    unsigned int msr;
    (void)model;
    msr = irq_off();
    gravar8((void *)ptr, val);
    irq_on(msr);
}

unsigned long long __atomic_exchange_8(volatile void *ptr, unsigned long long val, int model) {
    unsigned int msr;
    unsigned long long anterior;
    (void)model;
    msr = irq_off();
    anterior = ler8((const void *)ptr);
    gravar8((void *)ptr, val);
    irq_on(msr);
    return anterior;
}

unsigned long long __atomic_fetch_add_8(volatile void *ptr, unsigned long long val, int model) {
    unsigned int msr;
    unsigned long long anterior;
    (void)model;
    msr = irq_off();
    anterior = ler8((const void *)ptr);
    gravar8((void *)ptr, anterior + val);
    irq_on(msr);
    return anterior;
}

/* O quarto argumento é o `weak` que o builtin do gcc exige. O LLVM não o passa:
   o que chega ali é a ordem de memória, que esta implementação ignora. O valor
   comparado continua nos mesmos registradores. */
_Bool __atomic_compare_exchange_8(volatile void *ptr, void *expected, unsigned long long desired,
                                  _Bool weak, int success, int failure) {
    unsigned int msr;
    unsigned long long atual;
    unsigned long long queria;
    (void)weak;
    (void)success;
    (void)failure;
    msr = irq_off();
    atual = ler8((const void *)ptr);
    queria = ler8(expected);
    if (atual == queria) {
        gravar8((void *)ptr, desired);
        irq_on(msr);
        return 1;
    }
    gravar8(expected, atual);
    irq_on(msr);
    return 0;
}
