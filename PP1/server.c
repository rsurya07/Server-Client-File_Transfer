/************************************************************************
Name: Surya Ravikumar
Class: CS494
Date: 2/13/2018
Program: server program to send file to client
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

int received = 0;
double elapsed = 0;
int need_tosend = 0;
pthread_t request_thread;

struct timeval start, end;

struct sockaddr_in my_addr, client_addr;
socklen_t len = sizeof(client_addr);
int my_socket;

char buffer[1024];
int frame_number = 1;
int recvlen;

struct packet {
	int packet_number;
	int transactionId;
	int ack;
	int readSize;
	char data[1024];
};

struct packet transaction;
struct packet fromClient;

FILE * to_send;

void wait_for_client();
void wait_for_file();
void sendFile();
void re_send_data();

void * wait_for_ack();

int main(int argc, char * argv[])
{
	if(argc < 2)
	{
		printf("(-) Enter port number and re-run\n");
		return 0;
	}

	my_socket = socket(AF_INET, SOCK_DGRAM, 0);

	if(my_socket < 0)
	{
	     printf("\n(-) Error creating socket\n");
	     return -1;
	}


	bzero((char *) &my_addr, sizeof(my_addr));

	my_addr.sin_family = AF_INET;
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	my_addr.sin_port = htons(atoi(argv[1]));

	if(bind(my_socket, (struct sockaddr *) &my_addr, sizeof(my_addr)) < 0)
	{
		printf("\n(-) Bind Error\n");
		return -1;
	}

	printf("\n(+) Waiting on port: %s\n", argv[1]);

	wait_for_client();

	

	close(my_socket);
	
	return 0;

}

void wait_for_client()
{
/*
wait for client to connect and request for connection using packet number 1 and connection request as transaction Id
if matches, send ack and send transaction id as connection established
if not, then wait for client
*/
	recvlen = recvfrom(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &client_addr, &len);

	if(transaction.packet_number == 1 && transaction.transactionId == request_connection)
	{
		transaction.ack = 1;
		transaction.transactionId = connection_established;

		if(sendto(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &client_addr, len) < 0)
	     {
	          printf("\n(-) Send error\n");
	          return;
	     }
	
		printf("\n(+) Connected to Client: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

		 frame_number++;
		 wait_for_file();
	}

	else		
		wait_for_client();
}

void wait_for_file()
{
/*
see if packets received is seconf packet after connection request
receive file path from client and see if file exists
if exists, send ack and proceed to send file
if not, send ack, set transaction id to file not found and exit
*/
	recvlen = recvfrom(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &client_addr, &len);

	if(transaction.packet_number == 2 && transaction.transactionId == file_data)
	{
		transaction.ack = 1;
		
		to_send = fopen(transaction.data, "rb");

		if(to_send == NULL)
		{
			transaction.transactionId = file_not_found;
			printf("\n(-) File not found\n");

			if(sendto(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &client_addr, len) < 0)
	    	{
	          printf("\n(-) Send error\n");
	          return;
	     	}

			return;
		}

		printf("\n(+) File found\n");

		if(sendto(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &client_addr, len) < 0)
	     {
	          printf("\n(-) Send error\n");
	          return;
	     }

		frame_number++;
		transaction.packet_number = frame_number;
		sendFile();
	}

}



void sendFile()
{
/*
read data from file and send using re_send_data function
send transaction complete packet if all data is sent
*/
	while(!feof(to_send))
	{
		 need_tosend = 1;
		 received = 0;
		 transaction.packet_number = frame_number;
		 transaction.ack = 0;
	     bzero((char *) &(transaction.data), sizeof(transaction.data));		
	     transaction.readSize = fread(transaction.data, sizeof *(transaction.data), 1024, to_send);	
	     transaction.transactionId = file_data;
		 strcpy(buffer, transaction.data);

		 printf("\n*** FRAME %d ***\n", frame_number);

		 while(need_tosend)
			re_send_data();	     
	}

	transaction.packet_number = frame_number;
	transaction.transactionId = transfer_complete;
	printf("\n(+) Sending transfer end code\n");

	if(sendto(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &client_addr, len) < 0)
	     {
	          printf("\nEnd Comm Send error\n");
	          return;
	     }
	fclose(to_send);
}

void re_send_data()
{
/*
Send data to client
wait for ack
if ack received, proceed to next packet
if not, resend packet
*/
	received = 0;

	printf("(R)Sending Packet Number: %d\n", transaction.packet_number);

	if(sendto(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &client_addr, len) < 0)
	     {
	          printf("\nSend error\n");
	          return;
	     }

	if(pthread_create(&request_thread, NULL, wait_for_ack, NULL))
		printf("\nerror creating thread\n");

	gettimeofday(&start, NULL);
	gettimeofday(&end, NULL);
	elapsed = (end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec);

	while(elapsed < 100000)
	{
		gettimeofday(&end, NULL);

		elapsed = (end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec);		
	}
	
	if(!received)
	{
		if(!pthread_cancel(request_thread))
			printf("\n(-) Wait canceled\n");
	    	need_tosend = 1;
	}

	//pthread_join(request_thread, NULL);	

	if(fromClient.packet_number == frame_number && fromClient.transactionId == file_data && fromClient.ack)
		{
			printf("\n(+) Ack received from client\n");
			frame_number++;
			need_tosend = 0;
		}

	else if(!received)
		{
			printf("(-) Did not receive packet\n");
			need_tosend = 1;
		}	

	else if(fromClient.packet_number != frame_number || fromClient.transactionId != file_data)
		{
			printf("(-) Wrong packet received Packet Number: %d\n", fromClient.packet_number);
			need_tosend = 1;
		}			
}


void * wait_for_ack()
{
/*
Thread function that waits for ack while main thread runs timer
if file received set received to 1
if thread is canceled by main thread after delay, recieved is 0
*/
	received = 0;

	recvlen = recvfrom(my_socket, &fromClient, sizeof(fromClient), 0, (struct sockaddr *) &client_addr, &len);

	received = 1;
}
	
