/************************************************************************
Name: Surya Ravikumar
Class: CS494
Date: 2/13/2018
Program: client program to receiver file from server
************************************************************************/

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

int request_connection = 1;
int connection_established = 200;
int file_data = 100;
int transfer_complete = 101;
int file_not_found = 404;
int frame_number = 1;

double elapsed = 0;

pthread_t request_thread;

struct timeval start, end;

struct sockaddr_in server_addr;
socklen_t len = sizeof(server_addr);
int my_socket;

char buffer[1024];
int recvlen;
int received = 0;

struct packet {
	int packet_number;
	int transactionId;
	int ack;
	int readSize;
	char data[1024];
};

struct packet transaction;

void sendRequest();
void sendFile(char *);
void receiveFile(char *);

void * wait_for_ack();

int main(int argc, char * argv[])
{
	if(argc < 4)
	{
		printf("\n (-) Error: Enter IP, PORT and file path and re-run program\n\n");
		return 0;
	}

	my_socket = socket(AF_INET, SOCK_DGRAM, 0);

	if(my_socket < 0)
	{
		printf("\n(-) Socket Error\n");
		return -1;
	}

	bzero((char *) &server_addr, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(atoi(argv[2]));

	if(inet_aton(argv[1], &server_addr.sin_addr) == 0)
	{
		printf("\n(-) INET_ATON failed\n");
		return -1;
	}

	strcpy(buffer, argv[3]);
	
	sendRequest();
	
	close(my_socket);

	return 0;
}

void sendRequest()
{
/*
Send connection request to server and wait for ack
if ack proceed to send file path
if no ack, resend connection request
*/
	received = 0;

	transaction.packet_number = 1;
	transaction.transactionId = request_connection;
	transaction.ack = 0;

	if(sendto(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &server_addr, len) < 0)
	{
		printf("\n(-) Send error\n");
		return;
	}

	if(pthread_create(&request_thread, NULL, wait_for_ack, NULL))
		printf("\nerror creating thread\n");

	gettimeofday(&start, NULL);
	gettimeofday(&end, NULL);
	elapsed = (end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec);

	while(elapsed < 500000)
	{
		gettimeofday(&end, NULL);

		elapsed = (end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec);		
	}
	
	if(!received)
	{
		pthread_cancel(request_thread);
		printf("\n(-) ACK not received\n(-) Resending Connection Request\n");
	    sendRequest();
	}

	pthread_join(request_thread, NULL);
	
	if(transaction.ack == 1 && transaction.transactionId == connection_established && transaction.packet_number == 1)
	{
		printf("\n(+) Connection Established with Server\n");
		printf("Expecting packet: %d\nPacket Number: %d\n", frame_number, transaction.packet_number);
		
		frame_number++;

		sendFile(buffer);
	}

	else
	{
		printf("(-) Wrong Packet received\n(+) Retrying\n");
		printf("Expecting packet: %d\nPacket Number: %d\n", frame_number, transaction.packet_number);
		sendRequest();
	}

}

void sendFile(char * file)
{
/*
Send file path to server and wait for ack
if file exists, proceed to receive file
if file does not exist, exit program
if no ack, resend file path
*/
	received = 0;

	transaction.packet_number = 2;
	transaction.transactionId = file_data;
	transaction.ack = 0;
	strcpy(transaction.data, file);

	if(sendto(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &server_addr, len) < 0)
	{
		printf("\n(-) Send error\n");
		return;
	}

	if(pthread_create(&request_thread, NULL, wait_for_ack, NULL))
		printf("\nerror creating thread\n");

	gettimeofday(&start, NULL);
	gettimeofday(&end, NULL);
	elapsed = (end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec);

	while(elapsed < 500000)
	{
		gettimeofday(&end, NULL);

		elapsed = (end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec);		
	}
	
	if(!received)
	{
		pthread_cancel(request_thread);
		printf("\n(-) ACK not received\n(-) Resending Connection Request\n");
	    sendRequest();
	}

	pthread_join(request_thread, NULL);
	

	if(transaction.ack == 1 && transaction.packet_number == 2)
	{		
		if(transaction.transactionId == file_not_found)
		{
			printf("\n(-) File not found\n");
			printf("Expecting packet: %d\nPacket Number: %d\n", frame_number, transaction.packet_number);
			return;
		}

		else
		{
			printf("\n(+) File found\n");		     
			printf("Expecting packet: %d\nPacket Number: %d\n", frame_number, transaction.packet_number);

			frame_number++;

			receiveFile(buffer);
		}
	}

	else
	{
		printf("(-) Wrong Packet received\n(+) Retrying\n");
		sendFile(file);
	}

}	


void * wait_for_ack()
{
/*
Thread function that waits for ack whil main thread runs timer
*/
	received = 0;
	recvlen = recvfrom(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &server_addr, &len);
	received = 1;
}

void receiveFile(char * path)
{
/*
crreate or open file with file name mentioned in path
parse if entered as path
received packets from server
check if new data or duplicate data and write if new
send ack
*/
	char fileName[1024];
	int path_len = strlen(path);
	int space = 0;
	int write;

	for(int i = path_len - 1; i > 0; i--)
	{
		if(path[i] == '/')
		{
			space = i;
			break;
		}
	}

	if(space)
	{
		for(int i = 0; i < path_len - space; i++)
		{
			fileName[i] = path[space + i + 1];
			fileName[i+1] = '\0';
		}
	}

	else
		strcpy(fileName, path);
 
		FILE * store_file = fopen(fileName, "wb");

		if(store_file == NULL)
		{
			printf("(-) File open to write error\n");
			return;
		}

	bzero((char *) &(transaction.data), sizeof(transaction.data));

	while(recvfrom(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &server_addr, &len) > 0)
	{
		printf("\nExpecting packet: [%d]     Received Packet Number: [%d]\n", frame_number, transaction.packet_number);

		if(transaction.transactionId != transfer_complete)
   	     {		
			if(frame_number == transaction.packet_number && transaction.transactionId == file_data) //prevent duplicate writing if ack is lost
			{
				write = 1;
				frame_number++;

				transaction.ack = 1;

				if(sendto(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &server_addr, len) < 0)
				{
	  	 	  		printf("\nSend error\n");
	   	 	  		return;
				}
			}

			if(write)
			{
				printf("(+) New data written\n");		
	     		fwrite(transaction.data, transaction.readSize, sizeof *(transaction.data), store_file);
				write = 0;
			}

			else
				printf("(-) Duplicate data not written\n");

	     }
	
	     else
		 {
			printf("(+) Transfer Complete\n");
	     	break;
		 }
	}
	fclose(store_file);
		
}

	
