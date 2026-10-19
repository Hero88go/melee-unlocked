// Linux build: Winsock names over BSD sockets. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "windows.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#define ioctlsocket ioctl
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAEINPROGRESS EINPROGRESS
#define WSAEALREADY EALREADY
#define WSAECONNRESET ECONNRESET
#define WSAECONNREFUSED ECONNREFUSED
#define WSAETIMEDOUT ETIMEDOUT
#define WSAEMSGSIZE EMSGSIZE
#define WSAEINTR EINTR
#define WSAEADDRINUSE EADDRINUSE
#define WSAEISCONN EISCONN
#define WSAENOTCONN ENOTCONN
#define WSAEHOSTUNREACH EHOSTUNREACH
#define WSAENETUNREACH ENETUNREACH
#define SD_RECEIVE SHUT_RD
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR
typedef struct WSAData { WORD wVersion, wHighVersion; char szDescription[257], szSystemStatus[129]; } WSADATA;
inline int WSAStartup(WORD, WSADATA* data) { if (data) std::memset(data, 0, sizeof *data); return 0; }
inline int WSACleanup() { return 0; }
inline int WSAGetLastError() { return errno; }
inline void WSASetLastError(int error) { errno = error; }
typedef struct pollfd WSAPOLLFD;
inline int WSAPoll(WSAPOLLFD* fds, unsigned long count, int timeout) { return poll(fds, count, timeout); }
typedef unsigned long u_long;
typedef unsigned short u_short;
typedef struct sockaddr SOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct in_addr IN_ADDR;
// Winsock's setsockopt takes `const char*`; BSD takes `const void*`, which accepts either.
#ifndef SO_EXCLUSIVEADDRUSE
#define SO_EXCLUSIVEADDRUSE SO_REUSEADDR
#endif
