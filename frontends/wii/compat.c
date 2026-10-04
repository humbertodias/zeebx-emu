/* Nomes que a std do Linux referencia e o newlib do gcc 10.2 (a imagem de Wii
   do RetroArch) não exporta. São weak: um newlib mais novo que já os tenha
   ganha na hora do link. O RetroArch liga este .a com -mrvl; símbolo faltando
   quebra o DOL. Cada função devolve a falha que a std já sabe tratar, ou
   encaminha para o nome que o newlib realmente tem. O Broadway é um núcleo,
   então o pthread daqui guarda estado num só fio. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

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

#define FRACO __attribute__((weak))

/* Layout do stat64 do glibc no powerpc de 32 bits, com time_t de 32 bits.
   É o que a std lê. O stat do newlib tem outro formato, então os campos são
   copiados pelo nome. */
struct stat_glibc64 {
    unsigned long long st_dev;
    unsigned long long st_ino;
    unsigned int st_mode;
    unsigned int st_nlink;
    unsigned int st_uid;
    unsigned int st_gid;
    unsigned long long st_rdev;
    unsigned short pad2;
    long long st_size;
    long st_blksize;
    long long st_blocks;
    /* O newlib define st_atime como macro de st_atim.tv_sec. Os nomes daqui
       não podem ser esses, senão o campo vira um acesso com ponto. */
    long atime;
    long atime_nsec;
    long mtime;
    long mtime_nsec;
    long ctime;
    long ctime_nsec;
    unsigned long reservado4;
    unsigned long reservado5;
};

_Static_assert(sizeof(struct stat_glibc64) == 104, "stat64 do glibc no powerpc");
_Static_assert(offsetof(struct stat_glibc64, st_size) == 48, "st_size do stat64");

/* O_LARGEFILE do glibc no powerpc. O newlib não conhece esse bit. */
#define O_LARGEFILE_GLIBC 0x10000
#define AT_FDCWD_LINUX (-100)
#define AT_SYMLINK_NOFOLLOW_LINUX 0x100
#define AT_REMOVEDIR_LINUX 0x200

static void copia_stat(struct stat_glibc64 *para, const struct stat *de) {
    memset(para, 0, sizeof(*para));
    para->st_dev = (unsigned long long)de->st_dev;
    para->st_ino = (unsigned long long)de->st_ino;
    para->st_mode = de->st_mode;
    para->st_nlink = de->st_nlink;
    para->st_uid = de->st_uid;
    para->st_gid = de->st_gid;
    para->st_rdev = (unsigned long long)de->st_rdev;
    para->st_size = de->st_size;
    para->st_blksize = de->st_blksize;
    para->st_blocks = de->st_blocks;
    para->atime = de->st_atime;
    para->mtime = de->st_mtime;
    para->ctime = de->st_ctime;
}

static int abre(const char *caminho, int flags, int modo) {
    return open(caminho, flags & ~O_LARGEFILE_GLIBC, modo);
}

FRACO int open64(const char *caminho, int flags, ...) {
    int modo = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        modo = va_arg(ap, int);
        va_end(ap);
    }
    return abre(caminho, flags, modo);
}

FRACO int openat64(int dirfd, const char *caminho, int flags, ...) {
    int modo = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        modo = va_arg(ap, int);
        va_end(ap);
    }
    if (dirfd != AT_FDCWD_LINUX && caminho[0] != '/') {
        errno = ENOTSUP;
        return -1;
    }
    return abre(caminho, flags, modo);
}

FRACO int stat64(const char *caminho, struct stat_glibc64 *saida) {
    struct stat st;
    if (stat(caminho, &st) != 0) {
        return -1;
    }
    copia_stat(saida, &st);
    return 0;
}

FRACO int lstat64(const char *caminho, struct stat_glibc64 *saida) {
    struct stat st;
    if (lstat(caminho, &st) != 0) {
        return -1;
    }
    copia_stat(saida, &st);
    return 0;
}

FRACO int fstat64(int fd, struct stat_glibc64 *saida) {
    struct stat st;
    if (fstat(fd, &st) != 0) {
        return -1;
    }
    copia_stat(saida, &st);
    return 0;
}

FRACO int fstatat64(int dirfd, const char *caminho, struct stat_glibc64 *saida, int flags) {
    if (dirfd != AT_FDCWD_LINUX && caminho[0] != '/') {
        errno = ENOTSUP;
        return -1;
    }
    if (flags & AT_SYMLINK_NOFOLLOW_LINUX) {
        return lstat64(caminho, saida);
    }
    return stat64(caminho, saida);
}

FRACO long long lseek64(int fd, long long deslocamento, int origem) {
    return lseek(fd, (off_t)deslocamento, origem);
}

FRACO int ftruncate64(int fd, long long tamanho) { return ftruncate(fd, (off_t)tamanho); }

FRACO int unlinkat(int dirfd, const char *caminho, int flags) {
    if (dirfd != AT_FDCWD_LINUX) {
        errno = ENOTSUP;
        return -1;
    }
    if (flags & AT_REMOVEDIR_LINUX) {
        return rmdir(caminho);
    }
    return unlink(caminho);
}

struct dirent_glibc64 {
    unsigned long long d_ino;
    long long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
};

_Static_assert(offsetof(struct dirent_glibc64, d_name) == 19, "d_name do dirent64");

FRACO struct dirent_glibc64 *readdir64(DIR *dir) {
    struct dirent *entrada;
    static struct dirent_glibc64 saida;
    entrada = readdir(dir);
    if (!entrada) {
        return NULL;
    }
    memset(&saida, 0, sizeof(saida));
    saida.d_ino = (unsigned long long)entrada->d_ino;
    saida.d_reclen = sizeof(saida);
    strncpy(saida.d_name, entrada->d_name, sizeof(saida.d_name) - 1);
    return &saida;
}

struct iovec_glibc {
    void *iov_base;
    size_t iov_len;
};

FRACO ssize_t writev(int fd, const struct iovec_glibc *iov, int n) {
    ssize_t total = 0;
    int i;
    for (i = 0; i < n; i++) {
        ssize_t escrito = write(fd, iov[i].iov_base, iov[i].iov_len);
        if (escrito < 0) {
            return total > 0 ? total : escrito;
        }
        total += escrito;
    }
    return total;
}

/* SYS_futex, SYS_getrandom, SYS_copy_file_range e SYS_statx no powerpc. */
enum { SYS_FUTEX = 221, SYS_GETRANDOM = 359, SYS_COPY_FILE_RANGE = 379, SYS_STATX = 383 };

FRACO long syscall(long numero, long a1, long a2, long a3, long a4, long a5, long a6) {
    static unsigned int estado = 1;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    if (numero == SYS_GETRANDOM) {
        unsigned char *buf = (unsigned char *)a1;
        size_t i;
        size_t n = (size_t)a2;
        for (i = 0; i < n; i++) {
            estado = estado * 1664525u + 1013904223u;
            buf[i] = (unsigned char)(estado >> 24);
        }
        return (long)n;
    }
    /* statx e copy_file_range: a std cai no stat64 e no read/write. */
    if (numero == SYS_STATX || numero == SYS_COPY_FILE_RANGE || numero == SYS_FUTEX) {
        errno = ENOSYS;
        return -1;
    }
    errno = ENOSYS;
    return -1;
}

#define NCHAVES 64
struct chave_tls {
    int usada;
    void (*dtor)(void *);
    void *val;
};
static struct chave_tls chaves[NCHAVES];

FRACO int pthread_key_create(unsigned int *chave, void (*dtor)(void *)) {
    unsigned int i;
    for (i = 0; i < NCHAVES; i++) {
        if (!chaves[i].usada) {
            chaves[i].usada = 1;
            chaves[i].dtor = dtor;
            chaves[i].val = NULL;
            *chave = i;
            return 0;
        }
    }
    return EAGAIN;
}

FRACO int pthread_key_delete(unsigned int chave) {
    if (chave >= NCHAVES) {
        return EINVAL;
    }
    chaves[chave].usada = 0;
    chaves[chave].val = NULL;
    return 0;
}

FRACO int pthread_setspecific(unsigned int chave, const void *val) {
    if (chave >= NCHAVES || !chaves[chave].usada) {
        return EINVAL;
    }
    chaves[chave].val = (void *)val;
    return 0;
}

FRACO void *pthread_getspecific(unsigned int chave) {
    if (chave >= NCHAVES || !chaves[chave].usada) {
        return NULL;
    }
    return chaves[chave].val;
}

/* O primeiro inteiro do mutex é a contagem. Zero é livre. Há um fio só,
   então um mutex recursivo e um comum se comportam igual. */
FRACO int pthread_mutex_lock(void *mutex) {
    int *n = mutex;
    *n += 1;
    return 0;
}

FRACO int pthread_mutex_trylock(void *mutex) {
    int *n = mutex;
    *n += 1;
    return 0;
}

FRACO int pthread_mutex_unlock(void *mutex) {
    int *n = mutex;
    if (*n == 0) {
        return EPERM;
    }
    *n -= 1;
    return 0;
}

FRACO int pthread_mutex_init(void *mutex, const void *attr) {
    int *n = mutex;
    (void)attr;
    *n = 0;
    return 0;
}

FRACO int pthread_mutex_destroy(void *mutex) {
    int *n = mutex;
    *n = 0;
    return 0;
}

FRACO int pthread_mutexattr_init(void *attr) {
    (void)attr;
    return 0;
}

FRACO int pthread_mutexattr_destroy(void *attr) {
    (void)attr;
    return 0;
}

FRACO int pthread_mutexattr_settype(void *attr, int tipo) {
    (void)attr;
    (void)tipo;
    return 0;
}

FRACO int pthread_attr_init(void *attr) {
    (void)attr;
    return 0;
}

FRACO int pthread_attr_destroy(void *attr) {
    (void)attr;
    return 0;
}

FRACO int pthread_attr_setstacksize(void *attr, size_t tamanho) {
    (void)attr;
    (void)tamanho;
    return 0;
}

FRACO int pthread_create(void *thread, const void *attr, void *(*inicio)(void *), void *arg) {
    (void)thread;
    (void)attr;
    (void)inicio;
    (void)arg;
    errno = EAGAIN;
    return EAGAIN;
}

FRACO int pthread_detach(void *thread) {
    (void)thread;
    return 0;
}

FRACO int pthread_join(void *thread, void **valor) {
    (void)thread;
    (void)valor;
    return 0;
}

FRACO unsigned int pthread_self(void) { return 1; }

FRACO int pthread_setname_np(unsigned int thread, const char *nome) {
    (void)thread;
    (void)nome;
    return 0;
}

FRACO long sysconf(int nome) {
    if (nome == 30) {
        return 4096;
    }
    if (nome == 84) {
        return 1;
    }
    errno = EINVAL;
    return -1;
}

FRACO int sched_getaffinity(int pid, size_t tamanho, void *mascara) {
    (void)pid;
    (void)tamanho;
    (void)mascara;
    errno = ENOSYS;
    return -1;
}

FRACO void *dlsym(void *handle, const char *nome) {
    (void)handle;
    (void)nome;
    return NULL;
}

FRACO int fchown(int fd, uid_t uid, gid_t gid) {
    (void)fd;
    (void)uid;
    (void)gid;
    errno = ENOSYS;
    return -1;
}

FRACO uid_t geteuid(void) { return 0; }

FRACO ssize_t readlink(const char *caminho, char *buf, size_t tamanho) {
    (void)caminho;
    (void)buf;
    (void)tamanho;
    errno = ENOSYS;
    return -1;
}

FRACO char *realpath(const char *caminho, char *resolvido) {
    (void)caminho;
    (void)resolvido;
    errno = ENOSYS;
    return NULL;
}

FRACO int dirfd(DIR *dir) {
    (void)dir;
    errno = ENOTSUP;
    return -1;
}

FRACO DIR *fdopendir(int fd) {
    (void)fd;
    errno = ENOTSUP;
    return NULL;
}

FRACO void *mmap64(void *addr, size_t tamanho, int prot, int flags, int fd, long long deslocamento) {
    (void)addr;
    (void)tamanho;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)deslocamento;
    errno = ENOSYS;
    return (void *)-1;
}

FRACO ssize_t splice(int fd_in, long long *off_in, int fd_out, long long *off_out, size_t len,
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

FRACO ssize_t sendfile64(int out_fd, int in_fd, long long *offset, size_t count) {
    (void)out_fd;
    (void)in_fd;
    (void)offset;
    (void)count;
    errno = ENOSYS;
    return -1;
}

FRACO int ioctl(int fd, unsigned long pedido, ...) {
    (void)fd;
    (void)pedido;
    errno = ENOSYS;
    return -1;
}

FRACO int socket(int dominio, int tipo, int protocolo) {
    (void)dominio;
    (void)tipo;
    (void)protocolo;
    errno = ENOSYS;
    return -1;
}

FRACO int connect(int fd, const void *addr, unsigned int tamanho) {
    (void)fd;
    (void)addr;
    (void)tamanho;
    errno = ENOSYS;
    return -1;
}

FRACO int poll(void *fds, unsigned long n, int timeout) {
    (void)fds;
    (void)n;
    (void)timeout;
    errno = ENOSYS;
    return -1;
}

FRACO int getsockopt(int fd, int nivel, int nome, void *val, unsigned int *tamanho) {
    (void)fd;
    (void)nivel;
    (void)nome;
    (void)val;
    (void)tamanho;
    errno = ENOSYS;
    return -1;
}

FRACO int getaddrinfo(const char *no, const char *servico, const void *dicas, void **res) {
    (void)no;
    (void)servico;
    (void)dicas;
    (void)res;
    return -2;
}

FRACO void freeaddrinfo(void *res) { (void)res; }

FRACO const char *gai_strerror(int err) {
    (void)err;
    return "name resolution failed";
}

FRACO ssize_t recv(int fd, void *buf, size_t tamanho, int flags) {
    (void)fd;
    (void)buf;
    (void)tamanho;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

FRACO ssize_t send(int fd, const void *buf, size_t tamanho, int flags) {
    (void)fd;
    (void)buf;
    (void)tamanho;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

FRACO int setsockopt(int fd, int nivel, int nome, const void *val, unsigned int tamanho) {
    (void)fd;
    (void)nivel;
    (void)nome;
    (void)val;
    (void)tamanho;
    errno = ENOSYS;
    return -1;
}

FRACO ssize_t readv(int fd, const struct iovec_glibc *iov, int n) {
    ssize_t total = 0;
    int i;
    for (i = 0; i < n; i++) {
        ssize_t lido = read(fd, iov[i].iov_base, iov[i].iov_len);
        if (lido < 0) {
            return total > 0 ? total : lido;
        }
        total += lido;
        if ((size_t)lido < iov[i].iov_len) {
            break;
        }
    }
    return total;
}

/* A std chama isto no meio de um canal quando o outro lado ainda não
   entregou. Não há outro fio: voltar na hora evita dormir o quadro. */
FRACO int sched_yield(void) { return 0; }

/* thread::sleep passa flags 0 e um intervalo relativo. O retorno é o número
   do erro, não -1: a std compara com EINTR. */
FRACO int clock_nanosleep(int relogio, int flags, const struct timespec *pedido,
                          struct timespec *restante) {
    (void)relogio;
    if (!pedido) {
        return EINVAL;
    }
    if (flags & 1) {
        return 0;
    }
    if (nanosleep(pedido, restante) == 0) {
        return 0;
    }
    return errno ? errno : EINTR;
}

FRACO unsigned long getauxval(unsigned long tipo) {
    (void)tipo;
    return 0;
}

FRACO int sigaltstack(const void *nova, void *antiga) {
    (void)nova;
    (void)antiga;
    return 0;
}

FRACO int pause(void) {
    errno = EINTR;
    return -1;
}

FRACO const char *gnu_get_libc_version(void) { return "2.31"; }

FRACO int __res_init(void) { return 0; }
FRACO int res_init(void) { return 0; }
