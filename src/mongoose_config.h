#ifndef MONGOOSE_CONFIG_H
#define MONGOOSE_CONFIG_H

// Pico SDK arch + external lwIP sockets
#define MG_ARCH MG_ARCH_PICOSDK
#define MG_ENABLE_LWIP 1
#define MG_ENABLE_TCPIP 0
#define MG_ENABLE_WINSOCK 0

// Reasonable IO chunk size for Wi‑Fi
#ifndef MG_IO_SIZE
#define MG_IO_SIZE 1460
#endif

#endif