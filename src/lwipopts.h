#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// Include common settings
#include "lwipopts_examples_common.h"

#if !NO_SYS
// Thread stack sizes
#define TCPIP_THREAD_STACKSIZE 2048
#define DEFAULT_THREAD_STACKSIZE 2048

// Mailbox sizes - CRITICAL for socket operations
#define TCPIP_MBOX_SIZE 32
#define DEFAULT_RAW_RECVMBOX_SIZE 32
#define DEFAULT_TCP_RECVMBOX_SIZE 32
#define DEFAULT_UDP_RECVMBOX_SIZE 32
#define DEFAULT_ACCEPTMBOX_SIZE 32

// Memory pools - increase all of these
#define MEMP_NUM_NETBUF 16
#define MEMP_NUM_NETCONN 16
#define MEMP_NUM_TCPIP_MSG_INPKT 32
#define MEMP_NUM_TCPIP_MSG_API 32

// TCP PCBs
#define MEMP_NUM_TCP_PCB 16
#define MEMP_NUM_TCP_PCB_LISTEN 16

// Use libc malloc
#define MEM_LIBC_MALLOC 1

// Socket options
#define LWIP_SO_RCVBUF 1
#define LWIP_SO_RCVTIMEO 1
#define RECV_BUFSIZE_DEFAULT 2048

#ifndef SO_REUSE
#define SO_REUSE 1
#endif

// Core locking
#define LWIP_TCPIP_CORE_LOCKING_INPUT 1

// Time
#define LWIP_TIMEVAL_PRIVATE 0

#endif

#endif