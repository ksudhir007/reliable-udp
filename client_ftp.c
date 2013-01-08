#include "udp_utils.h"
#include "myftp.h"
#include <math.h>

typedef struct 
{
	int sock_fd;
	char fileName[BUFFER_SIZE];
	int windowSize;
	int seed;
	float probability;
	int meanTime;

}producer_args;

producer_args p_args;

struct myList *client_head=NULL;
static int sequence_tracker = 0;
int length_of_list;

pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
//pthread_cond_t count_max = PTHREAD_COND_INITIALIZER;
void* print_consumer(void *args);
pthread_t consumer_thread; 

int generate_random_number()
{
	return (-1) * log((float)drand48()) * p_args.meanTime;
}

void recieveFile(int sock_fd, char* fileName, int windowSize, int seed, float probability, int meanTime)
{
	struct iovec recv_packet[2], send_packet[2], iovrecv[2];
	int nbytes;
	struct timeval tv;
	struct myList *to_send_ts;
	struct header sendhdr, recvhdr;
	char recieved_buffer[FILEBUFSIZE];
	fd_set service_set, rset_last;
	int finCounter = 0;
	int finStart = -1;
	int lastSequence =-1;
	float random_dropper;

	p_args.sock_fd = sock_fd;
	p_args.windowSize = windowSize;
	memset(p_args.fileName, 0, BUFFER_SIZE);
	int length = strlen(fileName);
	memcpy(p_args.fileName, "RECVD_", 6);
	memcpy(p_args.fileName+6, fileName, length);
	p_args.seed = seed;
	p_args.probability = probability;
	p_args.meanTime = meanTime;

#if 0
	printf("Seed: %d\n", p_args.seed);
	printf("Probability: %f\n", p_args.probability);
	printf("Mean-Time: %d\n", p_args.meanTime);
#endif

	srand48(seed);

	pthread_create(&consumer_thread,NULL,print_consumer,NULL);

	while (1) 
	{
    		FD_ZERO(&service_set);
		FD_SET(p_args.sock_fd ,&service_set);

		tv.tv_sec = 5;
		tv.tv_usec = 0;
                        
		if(select(p_args.sock_fd + 1, &service_set, NULL, NULL, &tv) < 0)
		{
			if (errno == EINTR)
				continue;
		}

		if(FD_ISSET(p_args.sock_fd, &service_set))
		{
			memset((void*)&recvhdr, 0, sizeof(struct header));
			memset(recieved_buffer, 0, FILEBUFSIZE);
			recv_packet[0].iov_base = (void*)&recvhdr;
			recv_packet[0].iov_len = sizeof(struct header);
			recv_packet[1].iov_base = recieved_buffer;
			recv_packet[1].iov_len = sizeof(recieved_buffer);
				
			nbytes = readv(p_args.sock_fd, recv_packet,2);
							
			if(nbytes <= 0)
			{
				printf("ERROR: Server terminated. Quitting. \n ");
				goto done_with_this;
			}
///// Probing packet
			if (recvhdr.isLast == 2)
			{
				memset((void*)&sendhdr, 0, sizeof(struct header));
				sendhdr.isLast = 3;
				/*lock the variable*/
				pthread_mutex_lock(&count_mutex);			
				sendhdr.availWindow = p_args.windowSize - getLength(client_head);
				//pthread_cond_signal(&count_max);
				/*unlock the variable*/
				pthread_mutex_unlock(&count_mutex);

				send_packet[0].iov_base = (void*)&sendhdr;
				send_packet[0].iov_len = sizeof(struct header);
				send_packet[1].iov_base = NULL;
				send_packet[1].iov_len = 0;

				if(writev(p_args.sock_fd, send_packet, 2) < 0)
				{
					perror("Writev failed:");
					exit(1);
				}
				if (sendhdr.availWindow == 0)
					printf("ALERT! Window Size 0!\n");
				printf("Sent ACK for PROBE PACKET with available window size %d.\n", sendhdr.availWindow);
				continue;
			}
///// Probing packet
			if (recvhdr.seq > sequence_tracker)
			{
			   random_dropper = (float)drand48();
			   if(random_dropper >= p_args.probability)
			   {
				/* lock the variable */
				pthread_mutex_lock(&count_mutex);
	///////////////// CRITICAL SECTION
				int to_find = recvhdr.seq;
				if (NULL == (struct myList*)getNode(client_head, to_find) &&
					getLength(client_head) < p_args.windowSize && to_find > sequence_tracker)
				{
					client_head = (struct myList*) addToList(client_head, recv_packet);
					length_of_list = getLength(client_head);
					printf("Received packet %d. Adding it to receiving window.\n", recvhdr.seq);
					to_send_ts = (struct myList*)getNode(client_head, recvhdr.seq);
					if(recvhdr.isLast == myTRUE )
					{
						lastSequence = recvhdr.seq;
					}
						

				}

				if(recvhdr.seq == sequence_tracker + 1)
				{
					do
					{
						sequence_tracker++;

					}while(getNode(client_head, sequence_tracker + 1));
				}

				if(sequence_tracker == lastSequence)
				{
					finStart = 1;
					
				}
					

				printf("Packets in order till now : %d.\n", sequence_tracker);
resend_ack:
				memset((void*)&sendhdr, 0, sizeof(struct header));
				sendhdr.seq = sequence_tracker;
				sendhdr.isACK = 1;//myTRUE;
				if(to_send_ts != NULL)
				{
					sendhdr.ts = ((struct header*)to_send_ts->iv[0].iov_base)->ts;
					to_send_ts = NULL;
				}
				sendhdr.availWindow = p_args.windowSize - getLength(client_head);
	///////////////// CRITICAL SECTION	
				//pthread_cond_signal(&count_max);
				/*unlock the variable*/
				pthread_mutex_unlock(&count_mutex);

				send_packet[0].iov_base = (void*)&sendhdr;
				send_packet[0].iov_len = sizeof(struct header);
				send_packet[1].iov_base = NULL;
				send_packet[1].iov_len = 0;

			   random_dropper = (float)drand48();
			   if(random_dropper >= p_args.probability)
			   {
					if(writev(p_args.sock_fd, send_packet, 2) < 0)
					{
						perror("Writev failed:");
						exit(1);
					}
					if (sendhdr.availWindow == 0)
						printf("ALERT! Window Size 0!\n");
					printf("Sent ACK for packet %d with available window size = %d.\n", sendhdr.seq, sendhdr.availWindow);
				}
				else
				{
					printf("DROP!! Dropping ACK for the packet %d.\n", sendhdr.seq);
				}

				if (finStart == 1)
					goto retransmit_fin;
			   }
			   else
			   {
					printf("Dropping packet %d since generated random probability %f < %f.\n", recvhdr.seq, random_dropper, p_args.probability);
					continue;
			   }
			}
			else
			{
				printf("Duplicate packet %d arrived which is already consumed.\n", recvhdr.seq);
				goto resend_ack;
			}
		}
	}

		struct timeval time_count;
		FD_ZERO(&rset_last);
		int retransmitCounter = 0;

retransmit_fin:
	
		retransmitCounter++;
		if(retransmitCounter > 5)
		{
			goto done_with_this;
		}

 		FD_SET(p_args.sock_fd, &rset_last);
        time_count.tv_sec = 3;
        time_count.tv_usec = 0;

        if(select(p_args.sock_fd+1, &rset_last, NULL, NULL, &time_count) < 0)
        {
            if(errno == EINTR)
                goto retransmit_fin;
            else
            {
                perror("Select Error :");
                exit(1);
            }
        }
        if(FD_ISSET(p_args.sock_fd, &rset_last))
        {
            memset((void *)&recvhdr, 0, sizeof(struct header));
            memset(recieved_buffer, 0, FILEBUFSIZE);
            iovrecv[0].iov_base = (void*)&recvhdr;
            iovrecv[0].iov_len = sizeof(recvhdr);
            iovrecv[1].iov_base = recieved_buffer;
            iovrecv[1].iov_len = sizeof(recieved_buffer);
            if(readv(p_args.sock_fd, iovrecv, 2) < 0)
            {
                perror("Error while reading the ACK:");
                exit(1);
            }
            if(recvhdr.isLast == 1)
            {
            	if(writev(p_args.sock_fd, send_packet, 2) < 0)
		{
			perror("Writev failed:");
			exit(1);
		}
            }
        }
        else
        {
            goto retransmit_fin;
        }


done_with_this:
	printf("Exit main thread.\n");
	pthread_join(consumer_thread, NULL);
	printf("File received from server successfully and saved as %s\n", p_args.fileName);
	pthread_mutex_destroy(&count_mutex);
	//pthread_cond_destroy(&count_max);
}

void* print_consumer(void *args)
{
	int i, rc, counter;
	struct myList * temp_node;
	FILE *fp = NULL;
	int finished_doing = -1;
	int time_to_sleep = -1;

	fp = fopen(p_args.fileName, "w");
   	if(fp == NULL) 
	{
	    printf("ERROR! Unable to open file. \n");
	    return;
   	}

	for (;;) 
	{
		pthread_mutex_lock(&count_mutex);
		if (client_head != NULL)									
		{
			int toStart = ((struct header*)client_head->iv[0].iov_base)->seq;
			for(i = toStart; i<=sequence_tracker; i++)
			{
				temp_node = (struct myList*) getNode(client_head, i);


				int to_print_length = FILEBUFSIZE;
				if (temp_node != NULL)
				{
					
					printf("_________________PACKET %d DATA START_______________________\n", ((struct header*)temp_node->iv[0].iov_base)->seq);
					to_print_length = strlen((char *)temp_node->iv[1].iov_base) > FILEBUFSIZE ? FILEBUFSIZE : strlen((char *)temp_node->iv[1].iov_base);
					fwrite ((char *)temp_node->iv[1].iov_base , 1 , to_print_length, stdout);
					printf("\n");
					printf("_________________PACKET %d DATA END_________________________\n", ((struct header*)temp_node->iv[0].iov_base)->seq);

					if (((struct header*)temp_node->iv[0].iov_base)->isLast == 1)
					{
						finished_doing = 0;
					}
					fwrite ((char *)temp_node->iv[1].iov_base , 1 , to_print_length , fp);
					client_head = (struct myList*) deleteFromList(client_head, i);						

				}

				if( finished_doing == 0)
				{
					pthread_mutex_unlock(&count_mutex);
					goto lets_go_home;
				}
			}
		}
		/*unlock the variable*/
		pthread_mutex_unlock(&count_mutex);
		time_to_sleep = generate_random_number();
		printf("Print thread sleeping for %d milliseconds.\n", time_to_sleep);
		usleep(time_to_sleep * 1000);
	}

lets_go_home:
	fclose(fp);
	printf("Exit print thread.\n");

	pthread_exit(NULL);	
}

