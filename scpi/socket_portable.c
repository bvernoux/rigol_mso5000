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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "socket_portable.h"

void error(char *msg); /* shall be declared in user code */

#ifdef _WIN32
	extern BOOL WINAPI consoleHandler(DWORD signal); // shall be declared in user code
	/* strerror for WIN32 */
	char  msgbuf[256]; // for a message up to 255 bytes.

	int gettimeofday(struct timeval *tv, void* ignored)
	{
		FILETIME ft;
		unsigned __int64 tmp = 0;
		if (NULL != tv) {
			GetSystemTimeAsFileTime(&ft);
			tmp |= ft.dwHighDateTime;
			tmp <<= 32;
			tmp |= ft.dwLowDateTime;
			tmp /= 10;
			tmp -= 11644473600000000ULL;
			tv->tv_sec = (long)(tmp / 1000000UL);
			tv->tv_usec = (long)(tmp % 1000000UL);
		}
		return 0;
	}
#else
	extern void consoleHandler(int s); // shall be declared in user code
#endif

float TimevalDiff(const struct timeval *a, const struct timeval *b)
{
	return (a->tv_sec - b->tv_sec) + 1e-6f * (a->tv_usec - b->tv_usec);
}

void get_CurrentTime(char* date_time_ms, int date_time_ms_max_size)
{
	#define CURRENT_TIME_SIZE (30)
	char currentTime[CURRENT_TIME_SIZE+1] = "";
	time_t rawtime;
	struct tm * timeinfo;
  
	struct timeval curTime;
	gettimeofday(&curTime, NULL);
	int milli = curTime.tv_usec / 1000;
	
	time (&rawtime);
	timeinfo = localtime(&rawtime);
	
	strftime(currentTime, CURRENT_TIME_SIZE, "%Y-%m-%d %H:%M:%S", timeinfo);
	snprintf(date_time_ms, (date_time_ms_max_size-1), "%s.%03d", currentTime, milli);
}

void sleep_ms(int milliseconds) // Cross-platform sleep function
{
#ifdef _WIN32
        Sleep(milliseconds);
#else
        usleep(milliseconds * 1000);
#endif // win32
}

int sockInit(void)
{
  #ifdef _WIN32
	if (!SetConsoleCtrlHandler(consoleHandler, TRUE)) {
		fprintf(stderr, "\nERROR: Could not set control handler\n");
		fflush(stderr);		
		
		if(stdout)
		{
			fprintf(stdout, "\nERROR: Could not set control handler\n");
			fflush(stdout);
		}
		
		return 1;
	}
	
	WSADATA wsa_data;
	//return WSAStartup(MAKEWORD(1,1), &wsa_data);
	return WSAStartup(MAKEWORD(2,2), &wsa_data);
  #else
	signal(SIGINT, consoleHandler);

	return 0;
  #endif
}

int sockQuit(void)
{
#ifdef _WIN32
	return WSACleanup();
#else
	return 0;
#endif
}

int sockGetErrno(void)
{
#ifdef _WIN32	
	return WSAGetLastError();
#else
	return errno;
#endif	
}

void sockClose(int socket)
{
#ifdef _WIN32	
	shutdown(socket, SD_BOTH);
	sleep_ms(2000); // Wait Shutdown	
	closesocket(socket);
#else
	shutdown(socket, SHUT_RDWR);
	sleep_ms(2000); // Wait Shutdown
	close(socket);
#endif	
}

char* sockStrError(int error)
{
#ifdef _WIN32	
	msgbuf [0] = '\0';    // Microsoft doesn't guarantee this on man page.
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,   // flags
				   NULL,                // lpsource
				   error,               // message id
				   MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), // languageid
				   msgbuf,              // output buffer
				   sizeof (msgbuf),     // size of msgbuf, bytes
				   NULL);               // va_list of arguments
	return msgbuf;
#else
	return strerror(error);
#endif
}

/* Return optval */
int sockGetOpt(int socket, char* opt_name_ascii, int level, int opt_name)
{
	int optval;
	socklen_t optlen;
	
	/* Check the status for the optname option */
	optval = 0;
	optlen = sizeof(optval);
	if(getsockopt(socket, level, opt_name, (char*)&optval, &optlen) < 0)
	{
		printf("optname=%s\n", opt_name_ascii);
		error("Could not enable optname" );
	}
	//printf("optname=%s (optval=0x%08X, optlen=%d)\n", opt_name_ascii, optval, optlen);
	
	return optval;
}

void sockSetOpt(int socket, char* opt_name_ascii, int level, int opt_name, int opt_val)
{
	int optval;
	socklen_t optlen;
	int result;
	
	/* Check the status for the optname option */
	sockGetOpt(socket, opt_name_ascii, level, opt_name);
	
	optval = opt_val;
	optlen = sizeof(optval);

	printf("Set optname=%s to %d\n", opt_name_ascii, optval);
	result = setsockopt(socket, level, opt_name,
						(const char*)&optval,
						optlen);
	if ( result != 0 )
	{
		error("Could not enable optname" );
	}
	
	/* Check the status for the optname option */
	sockGetOpt(socket, opt_name_ascii, level, opt_name);
}

/* Return Timeout in seconds */
int sockGetOpt_Timeout(int socket, char* opt_name_ascii, int level, int opt_name)
{
	int ret;
	socklen_t optlen;

	/* Check the status for the optname option */
#ifdef _WIN32	
	DWORD optval;
	optval = 0;
	optlen = sizeof(optval);
#else
	struct timeval optval;
	optval.tv_sec = 0;
	optval.tv_usec = 0;
	optlen = sizeof(struct timeval);
#endif
	ret = getsockopt(socket, level, opt_name, (char*)&optval, &optlen);
	if(ret < 0)
	{
		printf("optname=%s\n", opt_name_ascii);	
		error("Could not enable optname" );
	}

#ifdef _WIN32
/*
	fprintf(stdout, "optname=%s (optval=0x%08X, optlen=%d)\n", opt_name_ascii, (int)optval, optlen);
	fflush(stdout);
	if(stdout)
	{
		fprintf(stdout, "optname=%s (optval=0x%08X, optlen=%d)\n", opt_name_ascii, (int)optval, optlen);
		fflush(stdout);
	}
*/
	ret = (optval / 1000);
#else
/*	
	fprintf(stdout, "optname=%s (optval.tv_sec=0x%08X, optval.tv_usec=0x%08X, optlen=%d)\n", opt_name_ascii, (int)optval.tv_sec, (int)optval.tv_usec, optlen);
	fflush(stdout);	
	if(stdout)
	{
		fprintf(stdout, "optname=%s (optval.tv_sec=0x%08X, optval.tv_usec=0x%08X, optlen=%d)\n", opt_name_ascii, (int)optval.tv_sec, (int)optval.tv_usec, optlen);
		fflush(stdout);
	}
*/	
	ret = optval.tv_sec;
#endif
	return ret;
}

/* https://stackoverflow.com/questions/2876024/linux-is-there-a-read-or-recv-from-socket-with-timeout */
void sockSetOpt_Timeout(int socket, char* opt_name_ascii, int level, int opt_name, int timeout_second)
{
	socklen_t optlen;
	int result;

#ifdef _WIN32	
	DWORD optval;
	optval = timeout_second * 1000;
	optlen = sizeof(optval);	
#else
	struct timeval optval;
	optval.tv_sec = timeout_second;
	optval.tv_usec = 0; // Not init'ing this can cause strange errors
	optlen = sizeof(struct timeval);	
#endif	
	
	/* Check the status for the optname option */
	sockGetOpt_Timeout(socket, opt_name_ascii, level, opt_name);

	printf("Set optname=%s to %d s\n", opt_name_ascii, timeout_second);
	result = setsockopt(socket, level, opt_name,
						(const char*)&optval,
						optlen);
	if ( result != 0 )
	{
		printf("optname=%s\n", opt_name_ascii);
		error("Could not enable optname" );
	}
	
	/* Check the status for the optname option */
	sockGetOpt_Timeout(socket, opt_name_ascii, level, opt_name);
/*
	result = sockGetOpt_Timeout(socket, opt_name_ascii, level, opt_name);
	fprintf(stdout, "Get optname %s = %d s\n", opt_name_ascii, result);
	fflush(stdout);
	if(stdout)
	{
		fprintf(stdout, "Get optname %s = %d s\n", opt_name_ascii, result);
		fflush(stdout);
	}
*/
}

/* Write n bytes to socket */
int socket_write_nbytes(int sockfd, unsigned char* buf, int nb_bytes)
{
	const unsigned char* p = buf;
	int bytes_left = nb_bytes;
	int nb_sent;
	int nb_total_send = 0;

	while((nb_sent = send(sockfd, (const char*)p, bytes_left, 0)) > 0)
	{
		nb_total_send += nb_sent;
		bytes_left -= nb_sent;
		p += nb_sent;
		if(bytes_left == 0)
			break;
	}

	if(nb_sent < 0)
	{
		printf("ERROR send failed (errno=%d)\n", errno);
		return nb_sent;
	}
	else if(nb_sent == 0)
	{
		printf("ERROR send failed (errno=%d)\n", errno);
		return nb_sent;
	}

	return nb_total_send;
}

/* Read n bytes from socket */
int socket_read_nbytes(int sockfd, unsigned char* buf, int nb_bytes)
{
	unsigned char* p = buf;
	int bytes_left = nb_bytes;
	int nb_read = 0;
	int nb_total_read = 0;

	while(1)
	{
		nb_read = recv(sockfd, (char*)p, bytes_left, MSG_WAITALL);
		if(nb_read < 0)
			break;

		bytes_left -= nb_read;
		nb_total_read += nb_read;
		p += nb_read;
		if(bytes_left == 0)
			break;
	}

	if(nb_read < 0)
	{
		printf("ERROR recv failed (errno=%d)\n", errno);
		return nb_read;
	}
	else if(nb_read == 0)
	{
		printf("ERROR recv failed ((errno=%d)\n", errno);
		return nb_read;
	}

	return nb_total_read;
}
