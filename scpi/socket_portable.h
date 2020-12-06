/*
 * Copyright (C) 2020 Benjamin VERNOUX
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef __SOCKET_PORTABLE_H__
#define __SOCKET_PORTABLE_H__

#include <time.h>

#ifdef __MINGW32__
  #include <fcntl.h> // for open
  #include <unistd.h> // for close
#endif

#ifdef _WIN32
	/* See http://stackoverflow.com/questions/12765743/getaddrinfo-on-win32 */
	#ifndef _WIN32_WINNT
		#define _WIN32_WINNT 0x0501  /* Windows XP. */
	#endif
	#include <winsock2.h>
	#include <Ws2tcpip.h>
	#define bzero(b,len) (memset((b), '\0', (len)), (void) 0)
	#define bcopy(b1,b2,len) (memmove((b2), (b1), (len)), (void) 0)
#else
	/* Assume that any non-Windows platform uses POSIX-style sockets instead. */
	#include <sys/socket.h>
	#include <netinet/tcp.h>
	#include <arpa/inet.h>
	#include <netdb.h>  /* Needed for getaddrinfo() and freeaddrinfo() */
	#include <unistd.h> /* Needed for close() */
	#include <errno.h>
	#include <signal.h>
	#include <unistd.h>
	#include <sys/time.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

// void error(char *msg);
// error() shall be defined in user code

void sleep_ms(int milliseconds);

#ifdef _WIN32
int gettimeofday(struct timeval *tv, void* ignored);
#endif
void get_CurrentTime(char* date_time_ms, int date_time_ms_max_size);
float TimevalDiff(const struct timeval *a, const struct timeval *b);

int sockInit(void);
int sockQuit(void);
int sockGetErrno(void);
void sockClose(int socket);
char* sockStrError(int error);

int sockGetOpt(int socket, char* opt_name_ascii, int level, int opt_name);
void sockSetOpt(int socket, char* opt_name_ascii, int level, int opt_name, int opt_val);

int sockGetOpt_Timeout(int socket, char* opt_name_ascii, int level, int opt_name);
void sockSetOpt_Timeout(int socket, char* opt_name_ascii, int level, int opt_name, int timeout_second);

int socket_write_nbytes(int sockfd, unsigned char* buf, int nb_bytes);
int socket_read_nbytes(int sockfd, unsigned char* buf, int nb_bytes);

#ifdef __cplusplus
}
#endif

#endif  /* __SOCKET_PORTABLE_H__ */
