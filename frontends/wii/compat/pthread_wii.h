/* O newlib do gcc 10.2 (a imagem de Wii do RetroArch) declara pthread_mutex_t
   e não define estes dois macros. O sqlite em THREADSAFE=1 precisa deles para
   inicializar os mutex estáticos. Se o cabeçalho já os tiver, não se mexe. */
#include <pthread.h>

#ifndef PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_MUTEX_INITIALIZER 0
#endif
#ifndef PTHREAD_MUTEX_RECURSIVE
#define PTHREAD_MUTEX_RECURSIVE 1
#endif
