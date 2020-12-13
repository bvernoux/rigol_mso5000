/*
 * A simple TCP client to test Rigol MSO5000 SCPI commands and retrieve waveforms (mainly to retrieve waveforms and check timings)
 */
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

#include <stdarg.h>
#include <inttypes.h>
#include <float.h>
#include <math.h>

#include "socket_portable.h"

#define APP_NAME "MSO5000_SCPI"
#define VERSION "v0.1.2 13/12/2020 B.VERNOUX"

#define BANNER1 APP_NAME " " VERSION "\n"
#define BANNER2 APP_NAME " <hostname> <port> [-n<nb_waveform>] [-f<waveform_rx_raw_data.bin>]\n"
#define SYNTAX "Syntax: " APP_NAME " <hostname or ip> <port> [-n<nb_waveform>] [-f<waveform_rx_raw_data_file (data received from server)>]\nExample:\n" APP_NAME " 10.23.73.21 5555 -n10000 -fwaveform_rx_raw_data.bin\nStop with Ctrl-C\n"

#define __USE_MINGW_ANSI_STDIO 1 // Required for MSYS2 mingw64 to support format "%z" ...

FILE* outfp = NULL;
int sockfd = -1;

#define CURR_TIME_SIZE (40)
char currTime[CURR_TIME_SIZE+1] = "";
struct timeval start_tv;
struct timeval curr_tv;

#define BUFSIZE (250000000) // Max 250 Millions samples on Rigol MSO5000
unsigned char* buf = NULL;
unsigned int total_bytes;
unsigned int packet_nb;

// Analog waveform data from WAV:PRE?
int format;
int type;
size_t npoints;
int unused3;
double sec_per_sample;
double xorigin;
double xreference;
double yincrement;
double yorigin;
double yreference;

#define FS_PER_SECOND ((double)1e15)
size_t fs_per_sample;

void error(char *msg);

void printf_dbg(const char *fmt, ...)
{
	va_list args;

	gettimeofday(&curr_tv, NULL);
	get_CurrentTime(currTime, CURR_TIME_SIZE);
	printf("%s (%05.03f s) ", currTime, TimevalDiff(&curr_tv, &start_tv));

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

void syntax(void)
{
	printf(BANNER2);
	printf(SYNTAX);
}

#ifdef _WIN32
BOOL WINAPI consoleHandler(DWORD signal)
{
	if (signal == CTRL_C_EVENT)
	{
		printf("\nCtrl-C pressed\nExit\n");
		error(NULL);
	}
	return TRUE;
}
#else
void consoleHandler(int s)
{
	if (s == SIGINT) // Ctrl-C
	{
		printf("\nCtrl-C pressed\nExit\n");
		error(NULL);
	}
}
#endif

void cleanup(void)
{
	if(outfp != NULL)
	{
		fclose(outfp);
		outfp = NULL;
	}

	if(sockfd != -1)
	{
		sockClose(sockfd);
		sockfd = -1;
	}

	sockQuit();

	if(buf != NULL)
	{
		free(buf);
		buf = NULL;
	}
}

/*
* error - wrapper for perror
*/
void error(char *msg)
{
	if(msg != NULL)
	{
		if(sockGetErrno() != 0)
		{
			printf("%s:\n %s\n", msg, sockStrError(sockGetErrno()));
		}else
		{
			printf("%s\n", msg);
		}
	}

	cleanup();

	exit(-1);
}

/* Return nb data read or 0 in case of error */
int read_chan_data(int chan)
{
	int read_nb, fwrite_nb;
	int nb_data;
	unsigned char str_buf[256];
	unsigned char buf_last_data[1];

	sprintf((char *)str_buf, ":WAV:DATA?\n");
	printf_dbg("%s", str_buf);
	socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));

	if(chan == 0)
	{
		sprintf((char *)str_buf, "*WAI\n");
		printf_dbg("%s", str_buf);
		socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));
	}

	/* receive header from the server */
	bzero(buf, 12);
	read_nb = socket_read_nbytes(sockfd, buf, 11);
	if(read_nb != 11)
	{
		printf_dbg("ERROR socket_read_nbytes() to read from socket\n");
		error("ERROR recv()");
	}
	nb_data = atoi((char *)&buf[2]);
	printf_dbg("WAV:DATA?=%s (nb_data=%d)\n", buf, nb_data);
	// Add robustness check to avoid buffer overflow/underflow
	if(nb_data < 0)
	{
		printf_dbg("Error nb_data(%d)< 0\n", nb_data);
		return 0;
	}
	if(nb_data >= BUFSIZE)
	{
		printf_dbg("Error nb_data(%d) >= BUFSIZE(%d)\n", nb_data, BUFSIZE);
		return 0;
	}

	int expected_nb_data = nb_data;
	int nb_total_read = 0;
	while(expected_nb_data > 0)
	{
		read_nb = recv(sockfd, (char *)(&buf[nb_total_read]), expected_nb_data, 0);
		if(read_nb == 0)
		{
			printf_dbg("\nEnd of file/connection closed by server\n");	
			return 0;
		}
		if(read_nb < 0)
		{
			printf_dbg("ERROR recv() to read from socket\n");
			return 0;
		}
		if(read_nb == 1) // Error
		{
			printf_dbg("Error RigolMSO5000 READ 0x%02X (expected %d data)\n", buf[nb_total_read], expected_nb_data);
			if(buf[nb_total_read] == 0x0A)
			{
				return 0;
			}
		}
		expected_nb_data -= read_nb;
		total_bytes += read_nb;
		nb_total_read += read_nb;
		packet_nb++;
		printf_dbg("packet_nb=%05d TotalBytes=%08d (recv=%04d nb_total_read=%04d)\n",
				packet_nb, total_bytes,
				read_nb, nb_total_read);
	}
	if(read_nb > 0)
	{
		/* Read last data 0x0A END */
		bzero(buf_last_data, 1);
		read_nb = socket_read_nbytes(sockfd, buf_last_data, 1);
		if(read_nb != 1)
		{
			printf_dbg("ERROR socket_read_nbytes() to read from socket\n");
			error("ERROR recv()");
		}

		//printf_dbg("End=0x%02X\n",	buf_last_data[0]);
		if(buf_last_data[0] == 0x0A)
		{
			/* Write data received to file */
			if(read_nb > 0)
			{
				if(outfp != NULL)
				{
					// double ydelta = yorigin + yreference;
					// float v = (float(buf[j]) - ydelta) * yincrement;
					fwrite_nb = fwrite(buf, sizeof(char), nb_total_read, outfp);
					if(fwrite_nb != nb_total_read)
					{
						printf_dbg("fwrite() on outfp error len=%d != expected %d\n", fwrite_nb, nb_total_read);
					}
				}
			}
			return nb_data;
		}	else
		{
			printf_dbg("ERROR RigolMSO5000 Read (first data=0x%02X) last data=0x%02X != 0x0A => flush all data\n", buf[0], buf_last_data[0]);
			read_nb = recv(sockfd, (char *)buf, BUFSIZE, 0);
			if(read_nb > 0)
			{
				printf_dbg("Flush %d data (last data=0x%02X)\n", read_nb, buf[read_nb-1]);
			} else
			{
				printf_dbg("Flush 0 data\n");
			}
			return 0;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	static unsigned char str_buf[255+1];
	struct sockaddr_in serveraddr;
	struct hostent *server;
	int portno;
	char *hostname;
	char *from_server_filename;
	int err_connect;
	int read_nb;
	int is_chan_enabled[4];
	int nb_chan;
	int nb_total_acq_failed;
	int nb_acq_failed;
	int nb_acq_failed_curr_chan;
	int nb_waveform = -1;
	int nb_retry = 10;
	int i;
	
	struct timeval start_acq;
	struct timeval curr_acq;

	float acq_time;
	float acq_time_min = FLT_MAX;
	float acq_time_max = FLT_MIN;
	float acq_time_sum = 0;

	struct timeval start_data;
	struct timeval curr_data;

	sockInit();

	printf(BANNER1);
	printf("Stop with Ctrl-C\n");
	printf("Parameters:\n");
	for(i = 0; i < argc; i++)
	{
		printf("%s ", argv[i]);
	}
	printf("\n");

	/* check command line arguments */
	if ( (argc < 3) || (argc > 5) ) 
	{
		syntax();
		exit(0);
	}
	hostname = argv[1];
	portno = atoi(argv[2]);
	if(argc > 3)
	{
		for(i = 3; i < argc; i++)
		{
			if(strncmp(argv[i], "-n", 2) == 0)
			{
				nb_waveform = atoi(&argv[i][2]);
				printf("nb_waveform: %d\n", nb_waveform);
			} else if(strncmp(argv[i], "-f", 2) == 0)
			{
				from_server_filename = &argv[i][2];
				printf("waveform_rx_raw_data_file: %s\n", from_server_filename);
				outfp = fopen(from_server_filename, "wb");
				if (outfp != NULL)
				{
					printf("waveform_rx_raw_data_file created OK\n");
				}
				else
				{
					printf("waveform_rx_raw_data_file error to create file: %s\n", from_server_filename);
					exit(-3);
				}
			} else 
			{
				printf("Error unknown argument %s\n", argv[i]);
				syntax();
				exit(-3);
			}
		}
	}

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
	{
		printf("ERROR socket return %d\n", sockfd);
		error("ERROR opening socket");
	}

	server = gethostbyname(hostname);
	if (server == NULL) {
		printf("ERROR no such host as %s\n", hostname);
		error("ERROR gethostbyname()");
	}

	/* build the server's Internet address */
	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	bcopy((char *)server->h_addr, (char *)&serveraddr.sin_addr.s_addr, server->h_length);
	serveraddr.sin_port = htons(portno);

	/* connect: create a connection with the server */
	err_connect = connect(sockfd, (const struct sockaddr*)&serveraddr, sizeof(serveraddr));
	if(err_connect < 0)
	{
		error("ERROR connect()");
	}

	// TCP_NODELAY = 1 => Disable Nagle's algorithm for send coalescing.
	sockSetOpt(sockfd, "TCP_NODELAY", IPPROTO_TCP, TCP_NODELAY, 1);
	int timeout_in_seconds = 40;
	// Set RX & TX Timeout to 40 seconds required to capture up to 200Mpts
	sockSetOpt_Timeout(sockfd, "SO_RCVTIMEO", SOL_SOCKET, SO_RCVTIMEO, timeout_in_seconds);
	sockSetOpt_Timeout(sockfd, "SO_SNDTIMEO", SOL_SOCKET, SO_SNDTIMEO, timeout_in_seconds);

	buf = malloc(BUFSIZE);
	if(buf == NULL)
	{
		error("ERROR malloc(BUFSIZE)");
	}

	total_bytes = 0;
	packet_nb = 0;

	gettimeofday(&start_tv, NULL);

	/* Clears all the event registers, and also clears the error queue. */
	sprintf((char *)str_buf, "*CLS\n");
	printf_dbg("%s", str_buf);
	socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));

	sprintf((char *)str_buf, ":STOP\n");
	printf_dbg("%s", str_buf);
	socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));

	/* Check Operation Complete */
	sprintf((char *)str_buf, "*OPC?\n");
	printf_dbg("%s", str_buf);
	socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));
	/* receive info from the server */
	bzero(buf, 21);
	read_nb = recv(sockfd, (char *)buf, 20, 0);
	if(read_nb <= 0)
	{
		printf_dbg("ERROR recv() to read from socket\n");
		error("ERROR recv()");
	}
	printf_dbg("*OPC?=%s", buf);
	/* Check Operation Complete */

	sprintf((char *)str_buf, "*IDN?\n");
	printf_dbg("%s", str_buf);
	socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));
	/* receive info from the server */
	bzero(buf, 201);
	read_nb = recv(sockfd, (char *)buf, 200, 0);
	if(read_nb <= 0)
	{
		printf_dbg("ERROR recv() to read from socket\n");
		error("ERROR recv()");
	}
	printf_dbg("IDN?=%s", buf);

	sprintf((char *)str_buf, ":WAV:MODE RAW\n");
	printf_dbg("%s", str_buf);
	socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));

	sprintf((char *)str_buf, ":WAV:FORM BYTE\n");
	printf_dbg("%s", str_buf);
	socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));

	/* Retrieve Channels details */
	nb_chan = 0;
	for(i = 0; i < 4; i++)
	{
		sprintf((char *)str_buf, ":CHAN%d:DISP?\n", i+1 );
		printf_dbg("%s", str_buf);
		socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));
		/* receive info from the server */
		bzero(buf, 11);
		read_nb = recv(sockfd, (char *)buf, 10, 0);
		if(read_nb <= 0)
		{
			printf_dbg("ERROR recv() to read from socket\n");
			error("ERROR recv()");
		}
		printf_dbg(":CHAN%d:DISP?=%s", i+1, buf);
		int chan_enabled = atoi((char *)buf);
		if(chan_enabled == 1)
		{
			is_chan_enabled[i] = 1;

			sprintf((char *)str_buf, ":WAV:SOUR CHAN%d\n", i+1);
			printf_dbg("%s", str_buf);
			socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));

			sprintf((char *)str_buf, ":WAV:PRE?\n");
			printf_dbg("%s", str_buf);
			socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));
			bzero(buf, 201);
			read_nb = recv(sockfd, (char *)buf, 200, 0);
			if(read_nb <= 0)
			{
				printf_dbg("ERROR recv() to read from socket\n");
				error("ERROR recv()");
			}
			printf_dbg(":WAV:PRE?=%s", buf);
			sscanf((char *)buf,
							"%d,%d,%zu,%d,%lf,%lf,%lf,%lf,%lf,%lf",
							&format,
							&type,
							&npoints,
							&unused3,
							&sec_per_sample,
							&xorigin,
							&xreference,
							&yincrement,
							&yorigin,
							&yreference);

			fs_per_sample = round(sec_per_sample * FS_PER_SECOND);
			printf_dbg("X: %zu points, %.8f origin, ref %.8f fs_per_sample %zu\n", npoints, xorigin, xreference, fs_per_sample);
			printf_dbg("Y: %.8f inc, %.8f origin, %.8f ref\n", yincrement, yorigin, yreference);

			nb_chan++;
		} else {
			is_chan_enabled[i] = 0;
		}
	}

	int nb_waveform_cnt = 0;
	nb_total_acq_failed = 0;
	while(nb_waveform != 0)
	{
		if(nb_waveform > 0)
			nb_waveform--;

		nb_waveform_cnt++;
		fprintf(stdout, "\nWaveform %d\n", nb_waveform_cnt);

		nb_acq_failed = 0;
		gettimeofday(&start_acq, NULL);

		sprintf((char *)str_buf, ":SING\n");
		printf_dbg("%s", str_buf);
		socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));

		while(1)
		{
			sprintf((char *)str_buf, ":TRIG:STAT?\n");
			printf_dbg("%s", str_buf);
			socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));

			sprintf((char *)str_buf, "*WAI\n");
			printf_dbg("%s", str_buf);
			socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));

			/* receive info from the server */
			bzero(buf, 21);
			read_nb = recv(sockfd, (char *)buf, 20, 0);
			if(read_nb <= 0)
			{
				printf_dbg("ERROR recv() to read from socket\n");
				error("ERROR recv()");
			}
			printf_dbg(":TRIG:STAT?=%s", buf);
			if( strncmp((char*)buf, "STOP", 4) == 0 )
				break;
		}

		for(i = 0; i < 4; i++)
		{
			nb_acq_failed_curr_chan = 0;
			if(is_chan_enabled[i] != 1)
				continue;

			sprintf((char *)str_buf, ":WAV:SOUR CHAN%d\n", i+1);
			printf_dbg("%s", str_buf);
			socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));

			if(i == 0)
			{
				sprintf((char *)str_buf, "*WAI\n");
				printf_dbg("%s", str_buf);
				socket_write_nbytes(sockfd, str_buf, strlen((char *)str_buf));
			}

			int retry;
			float time_diff_s;
			double speed_mbytes_per_sec;
			gettimeofday(&start_data, NULL);
			for(retry = 0; retry < nb_retry; retry++)
			{
				if(read_chan_data(i) > 0)
					break;
				nb_total_acq_failed++;
				nb_acq_failed++;
				nb_acq_failed_curr_chan++;
			}
			gettimeofday(&curr_data, NULL);
			get_CurrentTime(currTime, CURR_TIME_SIZE);
			time_diff_s = TimevalDiff(&curr_data, &start_data);
			speed_mbytes_per_sec = (float)(((double)npoints)/(1024.0*1024.0)) / time_diff_s;
			printf("%s CH%d read_chan_data %05.04f s, %zu pts, %05.03f MBytes/s (nb_acq_failed_curr_chan=%d)\n", currTime, i+1, time_diff_s, npoints, speed_mbytes_per_sec, nb_acq_failed_curr_chan);
		} // Analog channels loop

		gettimeofday(&curr_acq, NULL);
		acq_time = TimevalDiff(&curr_acq, &start_acq);
		acq_time_sum +=  acq_time;

		if(acq_time < acq_time_min)
			acq_time_min = acq_time;

		if(acq_time > acq_time_max)
			acq_time_max = acq_time;

		get_CurrentTime(currTime, CURR_TIME_SIZE);
		printf("%s Acq Time %05.04f s (%zu pts x %d chan, nb_acq_failed=%d)\n", currTime, acq_time, npoints, nb_chan, nb_acq_failed);
	} // end while

	get_CurrentTime(currTime, CURR_TIME_SIZE);

	float ack_time_avg_s;
	double speed_mbytes_per_sec;
	ack_time_avg_s = (acq_time_sum / nb_waveform_cnt);
	speed_mbytes_per_sec = (float)(((double)npoints*nb_chan)/(1024.0*1024.0)) / ack_time_avg_s;

	printf("\n%s Acq Time min=%05.04fs, max=%05.04fs, avg=%05.04fs(%05.03f MBytes/s), nb_waveform_cnt=%d, nb_total_acq_failed=%d\n", 
				currTime, acq_time_min, acq_time_max, ack_time_avg_s, speed_mbytes_per_sec, nb_waveform_cnt, nb_total_acq_failed);

	printf("\n");

	cleanup();

	return 0;
}
