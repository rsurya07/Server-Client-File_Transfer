To run server:

			./server (port) (max window size) (retransmission time in milliseconds)
	e.g:	./server 1080 10 100 		(window size is 10 packets and retransmission time is 100 milliseconds)


To run client:

			./client (IP address) (port number) (path of file)
	e.g:	./client 127.0.0.1 1080 /home/surya/Documents/report.pdf

Client will parse the path entered to store the recevied file

	e.g: 	if path is  /home/surya/Documents/report.pdf
			client program will parse report.pdf and store the received file 
			as report.pdf 
		
			if path is just a filename picture.jpg
			client program will store received file as picture.jpg
		
Client will write to the file only after receiving all the packets and transfer complete code


Resent packets may not always fill the window in the server program since window is not moved until 
the first packet in the window receives ack and other packets in the window may have already received 
their ack packets
	
	e.g:	if packets 11 12 13 14 15 16 are transmitted and max window size is 7
			and packets 12 13 14 15 16 receive acks, window size is incremented by 1 since packet 12 
			has been acked and incrementing window size by 1 takes the window size to max window size.
			The window range is now 11 - 17 (inclusive). Even though packets 13 14 15 16 have received their acks,
			window size is not incremented. Packets in link are 11 and 17 before their timeouts, and ack for 17 arrives
			before 11. Since 11 has still not been acked, window is not moved. When packets 11 is resent after time out,
			packet in link (packet 11) is 1 while window size (now has been halved since there was a timeout) is 3.
			Here packet in link != window size. After 11 receives it ack, window size is incremented to 4 and next 4 
			packets are sent to fill the window.


Window is shifted and incremented by 1 as soon as the first packet in window receives it ack

	e.g:	if packets 11 12 13 14 are sent and ack arrives for packets 11, window size is incremented by 1
			and the new window range is 12 - 16. Packets 15 and 16 are sent.
			

Window is halved per timer check if there is a timeout (single or mulitple)

	e.g: 	if packets 3 4 5 6 are sent (with max window size 5) and packets 5 has expired while all others have received acks,
			window size would have increased by 1 with each ack for packets 3, 4 and 6 and packets 7 8 9 would be sent with 
			window range 5 - 9 (reaching max window size 5). Even if ack arrives for packets 7 8 9 arrives, window size is not changed
			or moved since 5 is unacked. And after timeout for packets window is halved to size 2 and range is now 5 - 6. Only packet 5
			is resent (6 is already acked). When packet 5 receives its ack, window size is incremented to 3 and range is 10 - 12 (inclusive)
			
			if packets 3 4 5 6 are sent, and all packets time out in a single timeout check, window size is halved to 2 and only packets 3 
			and 4 are sent. If packets 3 and 4 are found to be expired in a timeout check, and packets 5 and 6 are found to expired in the
			next timeout check, window size is halved twice (since they did not expire during the same interval).
		
		
