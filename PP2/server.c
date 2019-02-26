#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <sys/wait.h>
#include <pthread.h>
#include <locale.h>

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#define request_connection 1
#define connection_established 200
#define file_data 100
#define transfer_complete 101
#define file_not_found 404

int lost = 0;
int transmitted = 0;
int number_of_packets = 0;
int file_size = 0;

struct timeval * s_time;
struct timeval e_time;
struct timeval begin;

pthread_t p_thread;
pthread_t t_thread;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

struct sockaddr_in my_addr, client_addr;
socklen_t len = sizeof(client_addr);
int rec_socket;

char buffer[1024];
int recvlen;
int window_size;
int base;
int upper;
int p_in_link;
int p_to_send;
double wait_time;
int max_window;
double elapsed;
int all_acked;
int last_sent;

#pragma pack(1)
struct packet {
	int packet_number;
	int transactionId;
	int ack;
	int readSize;
	char data[1024];
};
#pragma pack(0)

struct packet  transaction;
struct packet * toClient;
int * timeout;

FILE * to_send;

void loadPackets();
void wait_for_client();
void wait_for_file();
void sendFile();
void * packet_thread();
void * check_lost();

int main(int argc, char * argv[])
{
	if(argc != 4)
	{
		printf("Enter only (i) Port number (ii) window size (iii) Retransmission time in milliseconds and re-run\n");
		return 0;
	}
	
	if(atoi(argv[2]) < 1 || atof(argv[3]) <= 0)
	{
		printf("Enter valid window size and retransmission time and re-run\n");
		return 0;
	}
	
	rec_socket = socket(AF_INET, SOCK_DGRAM, 0);
	
	if(rec_socket < 0)
	{
	     printf("\n(-) Error creating socket\n");
	     return -1;
	}

	bzero((char *) &my_addr, sizeof(my_addr));

	my_addr.sin_family = AF_INET;
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	my_addr.sin_port = htons(atoi(argv[1]));

	if(bind(rec_socket, (struct sockaddr *) &my_addr, sizeof(my_addr)) < 0)
	{
		printf("\n(-) Bind Error\n");
		return -1;
	}

	printf("\n(+) Waiting on port: %s\n", argv[1]);

	max_window = atoi(argv[2]);
	wait_time = atof(argv[3]);
	
	wait_for_client();

	close(rec_socket);	
	
	return 0;
}

void wait_for_client()
{
//waits for a client to initiate connection
//if valid request made, send ack and waits to receive file

	recvlen = recvfrom(rec_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &client_addr, &len);

	if(transaction.packet_number == 1 && transaction.transactionId == request_connection)
	{
		transaction.ack = 1;
		transaction.transactionId = connection_established;

		if(sendto(rec_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &client_addr, len) < 0)
	     {
	          printf("\n(-) Send error\n");
	          return;
	     }
	
		printf("\n(+) Connected to Client: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

		wait_for_file();
	}

	else		
		wait_for_client();
}


void wait_for_file()
{
//waits for file request from client
//if file found, allocates memory to read file and send client number of packets to expect and ack
//if file not found, sends ack along with file not found id
//proceeds to send file or exit

	printf("\n(+) Waiting for file\n");
	
	recvlen = recvfrom(rec_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &client_addr, &len);
	
	if(transaction.packet_number == 2 && transaction.transactionId == file_data)
	{
		transaction.ack = 1;
		
		to_send = fopen(transaction.data, "rb");
		strcpy(buffer, transaction.data);

		if(to_send == NULL)
		{
			transaction.transactionId = file_not_found;
			printf("\n(-) File not found\n");

			if(sendto(rec_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &client_addr, len) < 0)
	    	{
	          printf("\n(-) Send error\n");
	          return;
	     	}

			return;
		}

		printf("\n(+) File found\n");
		
		//read file to allocate memory to store data in an array of packets
		while(!feof(to_send))
		{
		     file_size += fread(transaction.data, sizeof *(transaction.data), 1024, to_send);	
	    	 number_of_packets++;
	 	}
	
		transaction.readSize = number_of_packets;
		
		toClient = (struct packet *) calloc((number_of_packets + 1), sizeof(struct packet));
		timeout = (int *) malloc(sizeof(int) * (number_of_packets));
		s_time = (struct timeval *) calloc(number_of_packets, sizeof(struct timeval));
		
		if(sendto(rec_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &client_addr, len) < 0)
	     {
	          printf("\n(-) Send error\n");
	          return;
	     }

		fclose(to_send);
		loadPackets();
		
		free(toClient);
		free(timeout);
		free(s_time);	
	}
	
	else
		wait_for_file();
}

void loadPackets()
{
//reads the file and stores data along with other information in the array of packets
	to_send = fopen(buffer, "rb");

	if(to_send == NULL)
	{
		printf("\nNot able to open file for reading\n");
		return;
	}
	
	for(int i = 0; (i < number_of_packets) && (!feof(to_send)); i++)
	{
		timeout[i] = 1;
		toClient[i].packet_number = i;
		toClient[i].ack = 0;
		toClient[i].transactionId = file_data;
		
		bzero((char *) &(toClient[i].data), sizeof(toClient[i].data));	
		toClient[i].readSize = fread(toClient[i].data, sizeof *(toClient[i].data), 1024, to_send);	
	}
	
	bzero((char *) &(toClient[number_of_packets].data), sizeof(toClient[number_of_packets].data));	
	toClient[number_of_packets].packet_number = number_of_packets;
	toClient[number_of_packets].ack = 0;
	toClient[number_of_packets].transactionId = transfer_complete;
	
	fclose(to_send);
	
	sendFile();
}

void sendFile()
{
//send file

	base = 0;	
	p_in_link = 0;
	p_to_send = 0;
	window_size = 1;
	all_acked = 0;
	last_sent = 0;
	
	if(number_of_packets < window_size)
		window_size = number_of_packets;
		
	
	while(pthread_create(&p_thread, NULL, packet_thread, NULL));		//thread to received acks
	while(pthread_create(&t_thread, NULL, check_lost, NULL));			//thread to check for timed out packets
			
	gettimeofday(&begin, NULL);
	
	while(!all_acked)
	{			
		upper = base + window_size;
		
		if(upper > number_of_packets)
			upper = number_of_packets;
		
		for(p_to_send = base; p_to_send < upper; p_to_send++)
		{
			//send packets until window is full
			
			if(!toClient[p_to_send].ack && timeout[p_to_send] && p_in_link < window_size) 
			{
				printf("(+) Sending: %d     \n", p_to_send);
			
				gettimeofday(&(s_time[p_to_send]), NULL);
			
				if(sendto(rec_socket, &(toClient[p_to_send]), sizeof(toClient[p_to_send]), 0, (struct sockaddr *) &client_addr, len) < 0)
				{
					printf("\n(-) Send error\n");
					return;
				}
	
				timeout[p_to_send] = 0;
				++transmitted;
				++p_in_link;
				
				if(p_to_send == last_sent)
					++last_sent;
			}
		}
		
		printf("			Packets in link: %d\n			Window capacity: %d\n", p_in_link, window_size);
		printf("			Window range:    [%d -- %d] \n", base, upper-1);
		//window range is inclusive
		
		//wait for signal from timer or receiving thread
		pthread_mutex_lock(&lock);
		pthread_cond_wait(&cond, &lock);
		pthread_mutex_unlock(&lock);
	}	
	
	//cancel both threads and perform cleanup
	pthread_cancel(p_thread);
	pthread_join(p_thread, NULL);
	pthread_cancel(t_thread);
	pthread_join(t_thread, NULL);
	
	
	//send transfer end packet
	printf("\n\n(+) Sending transfer complete code (packet number): %d\n", p_to_send);
	
	if(sendto(rec_socket, &(toClient[number_of_packets]), sizeof(toClient[number_of_packets]), 0, (struct sockaddr *) &client_addr, len) < 0)
	{
	    	printf("\n(-) Send error\n");
	        return;
	}
	
	gettimeofday(&e_time, NULL);
	
	//printf statistics
	printf("\n\nTotal time taken to send file:  %.3f milliseconds\n", (e_time.tv_sec*1000 + e_time.tv_usec/1000.0) - (begin.tv_sec*1000 + begin.tv_usec/1000.0));
	
	setlocale(LC_NUMERIC, "");
	printf("Total file size to send:     	%'d bytes\n", file_size);
	printf("Total Data Packets to send:     %d of 1 KB each\n", number_of_packets);
	printf("Total Data Packets lost:        %d of 1 KB each\n", lost);
	printf("Total Data packets transmitted: %d of 1 KB each\n\n\n", transmitted);
	printf("Please note:\n             Resent packets may not always fill window since window is not moved unless packets that were sent the initially are acked\n\n             When all packets are sent and server waits for ack, packets in link will not fill window since there are no more packets to send will be reduced by 1 as each ack arrives\n\n");
}
	
void * packet_thread()
{
//thread to receive acks
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
	while(1)
	{
		recvlen = recvfrom(rec_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &client_addr, &len);
	
		//if the packet has not been already acked and has not expired then received the packet to process
		if((transaction.ack) && !(toClient[transaction.packet_number].ack) && !timeout[transaction.packet_number])
		{		
			//pthread_mutex_lock(&lock);		
			timeout[transaction.packet_number] = 0;
			toClient[transaction.packet_number].ack = 1;
			//pthread_mutex_unlock(&lock);			
							
			printf("\n										(+) Got ACK for: %d\n", transaction.packet_number);
					
			//move window base position if received packet is first in window			
			if(transaction.packet_number == base)
			{				
				for(int i = base + 1; i < number_of_packets; i++)
				{
					if(!toClient[i].ack)
					{
						base = i;	
						break;
					}
				}
				
				pthread_cond_signal(&cond);
			}		
				
			//since the packet is not in the link anymore, decrement packets in link	
			--p_in_link;
			
			//if window is not the the max value, increment
			if(window_size < max_window)
				++window_size;
					
			//update window range
			upper = base + window_size;
				
			if(upper > number_of_packets)
				upper = number_of_packets;
						
			usleep(5);				
		}
	}	
}
		
			
void * check_lost()
{
//thread to check for lost or expired packets

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	int acked = 1;
	int t = 0;
	int divide = 1;
	
	while(1)
	{
		//sleep till entered time
		usleep(wait_time*1000);
		
		acked = 1;
		t = 0;
		divide = 1;
			
		for(int i = base; i < max(last_sent, upper); i++)
		{	
			gettimeofday(&e_time, NULL);
		
			elapsed = (e_time.tv_sec*1000 + e_time.tv_usec/1000.0) - (s_time[i].tv_sec*1000 + s_time[i].tv_usec/1000.0);
		
			//check if expired
			if(elapsed > (wait_time - wait_time * 0.1) && !toClient[i].ack)// && !timeout[i])
			{		
				//if expired, set timeout
				
				//if the packet has timedout for the first time after sending/resending		
				if(!timeout[i])
				{
					//pthread_mutex_lock(&lock);					
					timeout[i] = 1;
					--p_in_link;			
					++lost;
			
					if(window_size > 1 && divide)
						window_size = window_size/2;
						
					divide = 0;
					
					printf("\n								(-) Timeout: %d\n", i);
					//pthread_mutex_unlock(&lock);
				}
				
				//variable set to send signal
				t = 1;	
			}			
		}
	
		//check if all packets have been acked
		for(int i = 0; i < number_of_packets; i++)
		{
			if(!toClient[i].ack)
			{
				acked = 0;
				break;
			}
		}
	
		all_acked = acked;
	
		//send signal if all have been acked, or there has been a timeout and if window is not full
		if((all_acked || t) && (p_in_link <= window_size))
			pthread_cond_signal(&cond);		
	}
}	
