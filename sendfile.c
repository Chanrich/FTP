// FTP transfer program - handles network links with loss rates of 20%.
// Tested on Deterlab 
//
// Develop by Richard Chan, Leo Linsky, Tim Ferrell
//
// ----File Transfer Usage-----
// Sending:        ./file s portnumber file
// Receving:       ./file r portnumber dest_hostname filesize
// ----------------------------
//

// For both
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// For server
#include <netdb.h>

// For client
#define WINDOW_SIZE 6500
#define BUF_SIZE 1400

int client(int portnum, char* hostname,int filesize)
{
	char* filename = "/tmp/rvFile";
	int client_socket;
	struct hostent *server;
	struct sockaddr_in serv_addr;
	socklen_t len = sizeof(serv_addr);
	if ( (client_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
		printf("Error when creating client socket\n");
		return 1;
	}
	
	server = gethostbyname(hostname);
	if ( server == NULL) {
		printf("Cannot retreive IP address\n");
		exit(0);
	}
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portnum); // port
	// Enter the IP address
	bcopy((char *)server->h_addr, 
	  (char *)&serv_addr.sin_addr.s_addr, server->h_length);
	remove(filename);
    FILE *fp = fopen(filename, "wb+");
    if(NULL == fp)
    {
        printf("Error opening file");
        return 1;
    }
	
	// Send Start to Server
	char msg_to_server[10] = "start";
	char *ACK_msg = "ACKm";
	char *REQ_msg = "REQm";
	sendto(client_socket, msg_to_server, 10, 0, (struct sockaddr*) &serv_addr, len );

	// Successful connection, set time out
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 400000;
	if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    	perror("Set Timeout Error");
	}	
    // Start receiving data
    int DataReceived  = 0;
    int TotalReceived = 0;
    unsigned long index=0;
    unsigned long tempindex=0;
    unsigned char buff[BUF_SIZE];
    int receive_file_buffer_size[WINDOW_SIZE]; 	// Store the receive packet size for each packet
    unsigned char receive_file_buffer[WINDOW_SIZE][BUF_SIZE];
    memset(buff, '\0', sizeof(buff));
    size_t DataWritten;
    size_t TotalWritten = 0;
	
	// Variables for lost packets situation
	int count = 0;
	unsigned int lost_packet_int_array_index = 0;
	int run_loop = 1;
	int current_buffer_bytes;
	int lost_packet_int_array[(BUF_SIZE-4)/sizeof(int)];
	memset(lost_packet_int_array, -1, sizeof(lost_packet_int_array));
    while(TotalWritten < filesize)
    {
		
  		int i;
  		memset(receive_file_buffer, '\0', WINDOW_SIZE*BUF_SIZE );
  		memset(receive_file_buffer_size, 0, sizeof(receive_file_buffer_size));
		
		while(run_loop > 0){
			while (1){
				// Try to get WINDOW_SIZE amount of packet, some might be lost 
				DataReceived = recvfrom(client_socket, buff, sizeof(buff), 0, (struct sockaddr*) &serv_addr, &len );
				if (DataReceived <= 0 ){
					//Timeout
					printf("Receive Timeout\n");
					break;
				} else if (DataReceived == 3){
					// non data packet
					DataReceived = recvfrom(client_socket, buff, sizeof(buff), 0, (struct sockaddr*) &serv_addr, &len );
				}
				TotalReceived += DataReceived;
				
				// Get the index number from the packet received
				memcpy(&tempindex,buff,sizeof(unsigned long));
				receive_file_buffer_size[tempindex] = DataReceived;
				memcpy(receive_file_buffer[tempindex], buff, DataReceived);
				//printf("\nDataReceived: %d bytes. Packet index: %lu. Total Data: %d bytes\n", receive_file_buffer_size[tempindex], tempindex, TotalReceived);

				// Check if we retrieve any lost packets, reduce the index
				int j;
				for (j = 0; j < lost_packet_int_array_index; j++){
					if (lost_packet_int_array[j] == tempindex){
						lost_packet_int_array_index--;
						int z;
						for (z = j; z < lost_packet_int_array_index; z++){
							lost_packet_int_array[z] = lost_packet_int_array[z+1];
						}
						lost_packet_int_array[lost_packet_int_array_index] = -1;
					}
				}
			}
			// Calculate total received bytes
			current_buffer_bytes = 0;
			for (i = 0; i < WINDOW_SIZE; i++){
				if (receive_file_buffer_size[i] != -1 && receive_file_buffer_size[i] != 0){
					current_buffer_bytes += receive_file_buffer_size[i] - 4;
				}
			}
			if (TotalWritten + current_buffer_bytes == filesize){
				//File is complete, quit
				run_loop = 0;
			}
			printf(" Real Data Received: %d, Current buffer:%d, in file:%d\n", TotalWritten + current_buffer_bytes, current_buffer_bytes,TotalWritten );
			
			// Check for lost packets
			for (i = 0; i < WINDOW_SIZE; i++){
				if (lost_packet_int_array_index >= ((BUF_SIZE-8)/sizeof(int)) ){
					// if lost packets total bytes is bigger than the buffer can hold, buffer has 4 bytes for header
					//printf("Lost packet reach limit, start sending replay msg\n");
					break;
				}
				if (receive_file_buffer_size[i] == 0){
					// Found a lost packet i
					// Record all the lost packets index, packet index can range from 0 ~ WINDOW_SIZE
					//printf("Packet %d is lost\n", i);
					receive_file_buffer_size[i] = -1;
					lost_packet_int_array[lost_packet_int_array_index] = i;
					lost_packet_int_array_index++;
				}
			}
			
			unsigned char replyMsg[BUF_SIZE];

			// exit condition
			if (run_loop == 0){
			 lost_packet_int_array_index = 0;
			}
			// Request lost packets, if lost occurs
			printf("Building reply msg for %d lost packets\n", lost_packet_int_array_index);
			memset(replyMsg, 0, sizeof(replyMsg));
			memcpy(replyMsg, REQ_msg, 4); // copy 4 bytes into buffer as header;
			memcpy(&replyMsg[4], &lost_packet_int_array[0], sizeof(lost_packet_int_array) );
			
			if (lost_packet_int_array_index > 0){
				//printf("Request packets:%s\n", replyMsg);
				sendto(client_socket, replyMsg, BUF_SIZE, 0, (struct sockaddr*) &serv_addr, len );
				run_loop = 1;
			} else {
				run_loop = 0;
			}
			
		}
		//Now write the buffer into the file
		count = 0;
		for (i = 0; i < WINDOW_SIZE; i++){
			if (receive_file_buffer_size[i] == 0  || receive_file_buffer_size[i] == -1){
				// Empty buffer, Don't write 
				count++;
			} 
			else {
		        if ((DataWritten=fwrite(&receive_file_buffer[i][4], sizeof(char), receive_file_buffer_size[i]-4,fp)) < 0){
					perror("Fwrite Error:");
				}
				TotalWritten += DataWritten;
				//printf("Data Written: %d\n",DataWritten);
				receive_file_buffer_size[i] = 0;
				//printf("Writing data: DataWritten:%d, i:%d\n", DataWritten, i);
			}
		}
		printf("Count: %d\n", count);
		// Clean the lost packet array as all data are stored
		memset(lost_packet_int_array, -1, sizeof(lost_packet_int_array));
		
		sendto(client_socket, ACK_msg, 10, 0, (struct sockaddr*) &serv_addr, len );
		while (recvfrom(client_socket, buff, sizeof(buff), 0, (struct sockaddr*) &serv_addr, &len ) > 0){
			run_loop = 1; //let me loop run again
			// do nothing
		}
		printf("TotalWritten: %d\n",TotalWritten);


    }
	int ack_loop;
	for (ack_loop = 0; ack_loop < 10; ack_loop++) {
		sendto(client_socket, ACK_msg, 10, 0, (struct sockaddr*) &serv_addr, len );
	}

	printf("File Received: %s\n", filename);
	printf("TotalReceived: %d\n",TotalReceived);
	//printf("TotalWritten: %d\n",TotalWritten);
	fclose(fp);
    return 0;
}


int server(int portnum, const char * filename)
{
	int server_socket, client_len;
    server_socket = socket(AF_INET, SOCK_DGRAM, 0);

    printf("Socket retrieve success\n");
    struct sockaddr_in serv_addr, client_addr;
    memset(&serv_addr, '0', sizeof(serv_addr));
	memset(&client_addr, '0', sizeof(client_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(portnum);

    if ( bind(server_socket, (struct sockaddr*)&serv_addr,sizeof(serv_addr)) != 0){
		printf("Error Binding \n");
	}
	printf("Bind successful.");
	
	// Wait for client to connect
	char msg_from_client[BUF_SIZE] = {0};
	client_len = sizeof(client_addr);
	printf("Waiting for client...\n");
	recvfrom(server_socket, msg_from_client, 100, 0, (struct sockaddr*) &client_addr, &client_len);
	printf("Received from client: %s\n", msg_from_client);
	
	//Start transmitting data

	//Open and record the size of the file
	FILE *fp = fopen(filename,"rb");
	long filesize = 0;
	if(fp==NULL)
	{
		printf("File open error");
		return 1;
	}
	fseek(fp, 0, SEEK_END);
	filesize = ftell(fp);
	printf("The size of %s is %lu\n", filename, filesize);
	fseek(fp, 0 ,SEEK_SET);

	// Packet index, made it 1 less than 0 as the loop will increment it by 1 at start
	unsigned long current_packet_index = 0;
	unsigned char buf_with_header[BUF_SIZE] = {0};
	int total_size_sent = 0;
	int total_read;
	printf("Start sending data to the client\n");



	// This buffer store the file 
	unsigned char partialFile[WINDOW_SIZE][BUF_SIZE-4];
	int read_loop = 0;
	int last_read_file_size, last_read_file_index;	//This will store the last read file size and its location
	int total_file_bytes_read_into_buffer = 0;

	int buffer_index = 0;

	while(total_file_bytes_read_into_buffer < filesize)
	{
		// Read the file into buffer at a chunk	
		last_read_file_index = WINDOW_SIZE+1;
		for (read_loop = 0; read_loop < WINDOW_SIZE; read_loop++){
			total_read = fread(partialFile[read_loop], sizeof(char), BUF_SIZE-4, fp);
			total_file_bytes_read_into_buffer += total_read;
			//printf("File read: %d, the partialFile size: %d\n", total_read, sizeof(partialFile[read_loop]));
			//printf("Total Read: %d\n", total_file_bytes_read_into_buffer);
			if (total_read < BUF_SIZE-4) {
				// At last bytes of file, record the buffer size
				last_read_file_size = total_read;
				last_read_file_index = read_loop;
				//printf("Last read file size:%d at :%d\n", last_read_file_size, last_read_file_index);
				break;
			}
		}
		int i;
		for (i = 0; i < WINDOW_SIZE; i++){
			// Clean the buffer
			memset(buf_with_header, '\0', sizeof(buf_with_header));
			// Merge newly read data into buf_with_header
			memcpy(buf_with_header,&current_packet_index,sizeof(unsigned long));
			memcpy(&buf_with_header[4], partialFile[i], sizeof(partialFile[i]));
			if (i != last_read_file_index) {
				total_read = sizeof(partialFile[i]);
			} else {
				total_read = last_read_file_size;
			}
			// Read something from the file, send it to the client
			if ( sendto(server_socket, buf_with_header, total_read+4,0, (struct sockaddr*) &client_addr,  sizeof( struct sockaddr)) <= 0) {
				// A error occur while write
				perror("Sendto");
				exit(1);
			}
			total_size_sent = total_size_sent + total_read;
			//printf("Sending packet with index: %lu and size:%d\n", current_packet_index, total_read+4);
			if (total_read < BUF_SIZE-4)
			{
				if (feof(fp)){
					printf("End of file\n");
				}
				if (ferror(fp)) {
					printf("Error reading\n");
				}
				break;
			}

			// Increment the packet index
			if (current_packet_index >= WINDOW_SIZE - 1){
				current_packet_index = 0;
			} else {
				current_packet_index++;
			}
		}

		// print the total size sent
		printf("%d bytes sent.\n", total_size_sent);
		
		// Now wait for the client to send reply msg, if end of line dont wait
		int loopflag = 0;
		while(loopflag == 0){
			printf("waiting for client\n");
			recvfrom(server_socket, msg_from_client, BUF_SIZE, 0, (struct sockaddr*) &client_addr, &client_len);
			// See which msg we got, ACK or REQ?
			if (strncmp("REQ", msg_from_client, 3) == 0){
				// Client request missing packets
				printf("Received a REQ msg\n");
				int lost_packets_int_array[WINDOW_SIZE] = {0};
				int array_loop,lost_packets_int_array_size = 0;
				// Save the lost packet number into int array
				for (array_loop = 0; array_loop < (BUF_SIZE-4)/sizeof(int); array_loop++){
					int tempnum;
					memcpy(&tempnum, &msg_from_client[4 + (sizeof(int)*array_loop)], sizeof(int));
					if (tempnum != -1) {
						//fount a packet number
						lost_packets_int_array_size++;
						lost_packets_int_array[array_loop] = tempnum;
					} else {
						break;
					}
				}
				
				for (array_loop = 0; array_loop < lost_packets_int_array_size; array_loop++){
					memset(buf_with_header, '\0', sizeof(buf_with_header));
					// Merge data into buf_with_header
					memcpy(buf_with_header,&lost_packets_int_array[array_loop],sizeof(unsigned long));
					memcpy(&buf_with_header[4], partialFile[lost_packets_int_array[array_loop]], sizeof(partialFile[lost_packets_int_array[array_loop]]));
					if (lost_packets_int_array[array_loop] != last_read_file_index) {
						total_read = sizeof(partialFile[lost_packets_int_array[array_loop]]);
					} else {
						total_read = last_read_file_size;
					}
					if (lost_packets_int_array[array_loop] <=  last_read_file_index){
						if ( sendto(server_socket, buf_with_header, total_read+4,0, (struct sockaddr*) &client_addr,  sizeof( struct sockaddr)) <= 0) {
							// A error occur while write
							perror("Sendto");
							exit(1);
						}
						// Send second time
						if ( sendto(server_socket, buf_with_header, total_read+4,0, (struct sockaddr*) &client_addr,  sizeof( struct sockaddr)) <= 0) {
							// A error occur while write
							perror("Sendto");
							exit(1);
						}
						// Send third time
						if ( sendto(server_socket, buf_with_header, total_read+4,0, (struct sockaddr*) &client_addr,  sizeof( struct sockaddr)) <= 0) {
							// A error occur while write
							perror("Sendto");
							exit(1);
						}
						total_size_sent = total_size_sent + total_read;
						//printf("Sending packet with index: %d and size:%d\n", lost_packets_int_array[array_loop], total_read+4);
					}
					//printf("lost_packet index :%d\n", lost_packets_int_array_size);
					if (total_read < BUF_SIZE-4)
					{
						break;
					}
				}
				printf("%d bytes sent.\n", total_size_sent);
				
			} else if (strncmp("ACK", msg_from_client, 3) == 0) {
				// If we receive a ACK msg, get out of the loop
				printf("Received: ACK\n");
				loopflag = 1;
				char *rcv = "rcv";
				sendto(server_socket, rcv, 3,0, (struct sockaddr*) &client_addr,  sizeof( struct sockaddr));
			}
			
		}
	}
	printf("Final Read returned: %d\n",total_read);
	printf("Total bytes sent : %d\n", total_size_sent);
	close(server_socket);

    return 0;
}


int main(int argc, char** argv)
{
	printf("----File Transfer Usage-----\nSending:\t\t %s s portnumber file filesize\nReceving:\t%s r portnumber dest_hostname filesize\n", argv[0], argv[0]);
	printf("----------------------------\n");
	const char* mode;
	if (argc != 5){
		printf("Invalid number of argument\n");
		return(1);
	}
	mode = argv[1];
    if (strcmp(mode, "r") == 0)
    {
		//Receiving 
		printf("--Receiver Mode\n");
		const int portnum = atoi(argv[2]);
		char* hostname = argv[3];
		int filesize = atoi(argv[4]);
		printf("Connecting to port %i at %s\n", portnum, hostname);
		//Call the client function with given port number.
		client(portnum, hostname,filesize);
    }
	else if (strcmp(mode, "s") == 0){
		//Sending Server
		printf("--Sending Mode\n");
		const int portnum = atoi(argv[2]);
		int filesize = atoi(argv[4]);
		printf("Connecting to port %i and opening %s\n", portnum, argv[3]);
		//Call the server function with given port number and file name.
		server(portnum, argv[3]);
	}
    else
    {
        printf("Invalid number of argument\n");
    }
    return 1; // Something went wrong
}
