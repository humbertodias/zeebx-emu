/* O newlib do devkitPPC não traz este cabeçalho. O sqlite amalgamado inclui
   ele sempre, mesmo quando a chamada de ioctl fica atrás de um ifdef que aqui
   não dispara. A declaração basta para o arquivo compilar. */
#ifndef ZEEBX_WII_SYS_IOCTL_H
#define ZEEBX_WII_SYS_IOCTL_H

int ioctl(int fd, unsigned long request, ...);

#endif
