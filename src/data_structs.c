#include "data_structs.h"

void messageQueue_init(struct messageQueue* Q)
{
	Q->front = 0;
	Q->rear = 0;
	Q->numMessages = 0;
}

void enqueue(struct messageQueue* Q, char* message, int socket_fd)
{
	if (Q->numMessages == QUEUE_SIZE)
	{
		printf("ERROR:MESSAGE_QUEUE_OVERFLOW\n");
		return;
	}
	strcpy(Q->messages[Q->rear],message);
	Q->client_socket_fds[Q->rear] = socket_fd;
	Q->rear = (Q->rear + 1) % QUEUE_SIZE;
	Q->numMessages++;
}

char* dequeue(struct messageQueue* Q, int* socket_fd)
{
	if (Q->numMessages == 0)
	{
		Q->front = 0;
		Q->rear = 0;
		return "ERROR:QUEUE_EMPTY\n";
	}
	Q->numMessages--;
	int index = Q->front;
	Q->front = (Q->front + 1) % QUEUE_SIZE;
	*socket_fd = Q->client_socket_fds[index];
	return Q->messages[index];
}

