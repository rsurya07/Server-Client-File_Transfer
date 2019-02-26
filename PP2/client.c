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

#define request_connection 1
#define connection_established 200
#define file_data 100
#define transfer_complete 101
#define file_not_found 404

int number_of_packets = 0;
int file_size = 0;

pthread_t request_thread;

struct sockaddr_in server_addr;
socklen_t len = sizeof(server_addr);
int my_socket;

char buffer[1024];
int received = 0;

#pragma pack(1)
struct packet {
	int packet_number;
	int transactionId;
	int ack;
	int readSize;
	char data[1024];
};
#pragma pack(0)

struct packet transaction;
struct packet * fromServer;

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
//send connection request and wait for ack
//if no ack received, resend packets
//if ack received, send file request

	received = 0;

	transaction.packet_number = 1;
	transaction.transactionId = request_connection;
	transaction.ack = 0;

	if(pthread_create(&request_thread, NULL, wait_for_ack, NULL))
			printf("\nerror creating thread\n");
			
	while(!received)
	{	
		if(sendto(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &server_addr, len) < 0)
		{
			printf("\n(-) Send error\n");
			return;
		}
		
		usleep(500000);
		
		if(!received)
			printf("\n(-) ACK not received\n(-) Resending Connection Request\n");
	}

	pthread_join(request_thread, NULL);
	
	if(transaction.ack == 1 && transaction.transactionId == connection_established && transaction.packet_number == 1)
	{
		printf("\n(+) Connection Established with Server\n");
		sendFile(buffer);
	}

	else
	{
		printf("(-) Wrong Packet received\n(+) Retrying\n");
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

	if(pthread_create(&request_thread, NULL, wait_for_ack, NULL))
			printf("\nerror creating thread\n");
			
	while(!received)
	{
		if(sendto(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &server_addr, len) < 0)
		{
			printf("\n(-) Send error\n");
			return;
		}
		
		usleep(500000);
		
		if(!received)
			printf("\n(-) ACK not received\n(-) Resending File Request\n"); 
	}

	pthread_join(request_thread, NULL);
	

	if(transaction.ack == 1 && transaction.packet_number == 2)
	{		
		if(transaction.transactionId == file_not_found)
		{
			printf("\n(-) File not found\n\n\n");
			return;
		}

		else
		{
			printf("\n(+) File found\n\n\n");		     
			fromServer = (struct packet *) calloc (transaction.readSize, sizeof(struct packet));
			number_of_packets = transaction.readSize;

			receiveFile(buffer);
			free(fromServer);
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
Thread function that waits for ack whil main thread sleeps
*/
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	
	received = 0;
	
	recvfrom(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &server_addr, &len);
	
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

	for(int i = 0; i < number_of_packets; i++)	
	{
		fromServer[i].packet_number = i;
		fromServer[i].ack = 0;
		fromServer[i].transactionId = 0;
		fromServer[i].readSize = 0;
		bzero((char *) &(fromServer[i].data), sizeof(fromServer[i].data));
	}

	
	while(recvfrom(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &server_addr, &len) > 0)
	{
		 if((transaction.packet_number >=0) && (transaction.packet_number < number_of_packets) && (transaction.transactionId == file_data))
   	     {		
			if(!fromServer[transaction.packet_number].ack)
			{				
				int in = transaction.packet_number;
				printf("(+) New data received: %d\n", in);
				transaction.ack = 1;
				
				
				//random sleep to send ack packets late to check how server program responds
				
				//srand(time(0));
				//usleep(rand() % 150000 + 1);		
				
				if(sendto(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &server_addr, len) < 0)
	  	 	  		printf("\nSend error\n");	
	  	 	  		
				fromServer[in].transactionId = transaction.transactionId;
				fromServer[in].readSize = transaction.readSize;
				fromServer[in].ack = 1;
				memcpy(&(fromServer[in].data), &(transaction.data), sizeof(transaction.data));
				file_size += fromServer[in].readSize;			
			}

			else
			{
				transaction.ack = 1;
				
				if(sendto(my_socket, &transaction, sizeof(transaction), 0, (struct sockaddr *) &server_addr, len) < 0)
	  	 	  		printf("\nSend error\n");

				printf("						(-) Duplicate data received: %d \n", transaction.packet_number);
			}
	     }
    	
    	 else if(transaction.transactionId == transfer_complete)
		 {
		 	printf("\n\n(+) Transfer Complete: %d\n\n", transaction.packet_number);			
	     	break;
		 }
	}		
	
	printf("(+) Saving file as: %s\n\n", fileName);
	setlocale(LC_NUMERIC, "");
	printf("(+) Received file size: %'d bytes\n\n\n", file_size);
	
	for(int i = 0; i < number_of_packets; i++)				
		fwrite(fromServer[i].data, fromServer[i].readSize, sizeof *(fromServer[i].data), store_file);

	fclose(store_file);	
}

