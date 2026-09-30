#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "data_structs.h"
#include "socket_utils.h"

static struct acceptedClientSocketFDs* head;
static struct messageQueue* received_messages;
static sem_t queue_mutex;
static sem_t empty;
static sem_t full;
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static int wait_for_semaphore(sem_t* semaphore)
{
	while (sem_wait(semaphore) < 0)
	{
		if (errno != EINTR)
			return -1;
	}
	return 0;
}

static void remove_client(int socket_fd)
{
	pthread_mutex_lock(&clients_mutex);
	struct acceptedClientSocketFDs** current = &head->next;
	while (*current != NULL)
	{
		if ((*current)->value == socket_fd)
		{
			struct acceptedClientSocketFDs* removed = *current;
			*current = removed->next;
			free(removed);
			break;
		}
		current = &(*current)->next;
	}
	pthread_mutex_unlock(&clients_mutex);
}

static int send_all(int socket_fd, const char* data, size_t size)
{
	size_t sent = 0;
	while (sent < size)
	{
		ssize_t result = send(socket_fd, data + sent, size - sent, MSG_NOSIGNAL);
		if (result < 0 && errno == EINTR)
			continue;
		if (result <= 0)
			return -1;
		sent += (size_t)result;
	}
	return 0;
}

static int enqueue_message(const char* message, int socket_fd)
{
	if (wait_for_semaphore(&empty) < 0)
		return -1;
	if (wait_for_semaphore(&queue_mutex) < 0)
	{
		sem_post(&empty);
		return -1;
	}
	enqueue(received_messages, (char*)message, socket_fd);
	sem_post(&queue_mutex);
	sem_post(&full);
	return 0;
}

static void* handle_client(void* arg)
{
	int client_socket_fd = *(int*)arg;
	free(arg);

	char input[512];
	char message[MESSAGE_BUFFER_SIZE];
	size_t message_size = 0;
	bool discarding_message = false;

	for (;;)
	{
		ssize_t bytes_read = read(client_socket_fd, input, sizeof(input));
		if (bytes_read < 0 && errno == EINTR)
			continue;
		if (bytes_read <= 0)
			break;

		for (ssize_t index = 0; index < bytes_read; ++index)
		{
			char character = input[index];
			if (character == '\n')
			{
				if (!discarding_message)
				{
					if (message_size > 0 && message[message_size - 1] == '\r')
						--message_size;
					if (message_size > 0)
					{
						message[message_size] = '\0';
						if (enqueue_message(message, client_socket_fd) < 0)
							goto finished;
					}
				}
				message_size = 0;
				discarding_message = false;
			}
			else if (!discarding_message)
			{
				if (character == '\0' || message_size == MAX_CHAT_MESSAGE_SIZE)
				{
					message_size = 0;
					discarding_message = true;
				}
				else
					message[message_size++] = character;
			}
		}
	}

finished:
	remove_client(client_socket_fd);
	close(client_socket_fd);
	return NULL;
}

static void* send_messages(void* arg)
{
	(void)arg;
	for (;;)
	{
		if (wait_for_semaphore(&full) < 0 || wait_for_semaphore(&queue_mutex) < 0)
			continue;

		int sender_fd = -1;
		char message[MESSAGE_BUFFER_SIZE];
		snprintf(message, sizeof(message), "%s", dequeue(received_messages, &sender_fd));
		sem_post(&queue_mutex);
		sem_post(&empty);

		size_t message_size = strlen(message);
		char frame[MESSAGE_BUFFER_SIZE];
		memcpy(frame, message, message_size);
		frame[message_size++] = '\n';

		pthread_mutex_lock(&clients_mutex);
		for (struct acceptedClientSocketFDs* client = head->next; client != NULL; client = client->next)
		{
			if (client->value != sender_fd)
				send_all(client->value, frame, message_size);
		}
		pthread_mutex_unlock(&clients_mutex);
	}
	return NULL;
}

int main(int argc, char** argv)
{
	int port = 2000;
	if (argc == 2)
	{
		char* end = NULL;
		errno = 0;
		long parsed_port = strtol(argv[1], &end, 10);
		if (errno != 0 || end == argv[1] || *end != '\0' || parsed_port < 1 || parsed_port > 65535)
		{
			fprintf(stderr, "Usage: %s [port]\n", argv[0]);
			return EXIT_FAILURE;
		}
		port = (int)parsed_port;
	}
	else if (argc != 1)
	{
		fprintf(stderr, "Usage: %s [port]\n", argv[0]);
		return EXIT_FAILURE;
	}

	head = calloc(1, sizeof(*head));
	received_messages = malloc(sizeof(*received_messages));
	if (head == NULL || received_messages == NULL)
	{
		perror("allocate server state");
		return EXIT_FAILURE;
	}
	messageQueue_init(received_messages);
	if (sem_init(&queue_mutex, 0, 1) < 0 ||
		sem_init(&empty, 0, QUEUE_SIZE) < 0 ||
		sem_init(&full, 0, 0) < 0)
	{
		perror("sem_init");
		return EXIT_FAILURE;
	}

	int socket_fd = createTCPIpv4Socket();
	if (socket_fd < 0)
	{
		perror("socket");
		return EXIT_FAILURE;
	}
	int reuse_address = 1;
	setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address));

	struct sockaddr* address = createTCPIpv4SocketAddress("", port);
	if (address == NULL || bind(socket_fd, address, sizeof(struct sockaddr_in)) < 0)
	{
		perror("bind");
		free(address);
		close(socket_fd);
		return EXIT_FAILURE;
	}
	free(address);
	if (listen(socket_fd, 10) < 0)
	{
		perror("listen");
		close(socket_fd);
		return EXIT_FAILURE;
	}
	printf("Listening on port %d\n", port);

	pthread_t sender_thread;
	int thread_error = pthread_create(&sender_thread, NULL, send_messages, NULL);
	if (thread_error != 0)
	{
		errno = thread_error;
		perror("pthread_create");
		close(socket_fd);
		return EXIT_FAILURE;
	}
	pthread_detach(sender_thread);

	for (;;)
	{
		struct sockaddr_storage client_address;
		socklen_t address_size = sizeof(client_address);
		int client_socket_fd = accept(socket_fd, (struct sockaddr*)&client_address, &address_size);
		if (client_socket_fd < 0)
		{
			if (errno == EINTR)
				continue;
			perror("accept");
			break;
		}

		struct acceptedClientSocketFDs* client = malloc(sizeof(*client));
		int* thread_socket_fd = malloc(sizeof(*thread_socket_fd));
		if (client == NULL || thread_socket_fd == NULL)
		{
			perror("allocate client state");
			free(client);
			free(thread_socket_fd);
			close(client_socket_fd);
			continue;
		}
		client->value = client_socket_fd;
		pthread_mutex_lock(&clients_mutex);
		client->next = head->next;
		head->next = client;
		pthread_mutex_unlock(&clients_mutex);

		*thread_socket_fd = client_socket_fd;
		pthread_t client_thread;
		thread_error = pthread_create(&client_thread, NULL, handle_client, thread_socket_fd);
		if (thread_error != 0)
		{
			errno = thread_error;
			perror("pthread_create");
			free(thread_socket_fd);
			remove_client(client_socket_fd);
			close(client_socket_fd);
			continue;
		}
		pthread_detach(client_thread);
	}

	close(socket_fd);
	return EXIT_FAILURE;
}
