#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
/*
https://www.ibm.com/docs/en/ztpf/1.1.0.15?topic=functions-structures-defined-in-syssocketh-header-file
*/
#include <netinet/in.h>
/*The netinet/in.h header file contains definitions for the internet protocol family.
https://www.ibm.com/docs/en/zos/2.4.0?topic=files-netinetinh-internet-protocol-family
*/
#include <arpa/inet.h>
/*
arpa/inet.h - definitions for internet operations
http://manpages.ubuntu.com/manpages/trusty/man7/inet.h.7posix.html
*/
/*
The sys/types. h header file defines a collection of typedef symbols and structures.
https://docs.oracle.com/cd/E19253-01/816-5138/convert-6/index.html
*/
#include <signal.h>
/*
signal. h is a header file defined in the C Standard Library to specify how a program handles signals while it executes.
https://www.tutorialspoint.com/c_standard_library/signal_h.htm
*/
#include <errno.h>
/*
The errno.h header file of the C Standard Library defines the integer variable errno, which is set by system calls and some library
functions in the event of an error to indicate what went wrong.
https://www.tutorialspoint.com/c_standard_library/errorno_h.htm
*/

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define NAME_LEN 32

static _Atomic unsigned int cli_count = 0;
// Atomic variables can be accessed concurrently between different threads without creating race conditions.
static int uid = 10;

// Client structure will store information about client's address,name, id
typedef struct
{
    struct sockaddr_in address;
    int sockfd;
    int uid;
    char name[NAME_LEN];
} client_t;

client_t *clients[MAX_CLIENTS];

int flag = 0;

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void str_overwrite_stdout()
{
    printf("\r%s", "> ");
    fflush(stdout);
}

void str_trim_lf(char *arr, int length)
{
    for (int i = 0; i < length; i++)
    {
        if (arr[i] == '\n')
        {
            arr[i] = '\0';
            break;
        }
    }
}

// add the client to the queue
void queue_add(client_t *cl)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (!clients[i])
        {
            clients[i] = cl;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

// remove the client from the queue
void queue_remove(int uid)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i])
        {
            if (clients[i]->uid == uid)
            {
                clients[i] = NULL;
                break;
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

void print_ip_addr(struct sockaddr_in addr, int port)
{
    printf("Current IP Address of the server is : ");
    printf("%d.%d.%d.%d\n",
           addr.sin_addr.s_addr & 0xff,
           (addr.sin_addr.s_addr & 0xff00) >> 8,
           (addr.sin_addr.s_addr & 0xff0000) >> 16,
           (addr.sin_addr.s_addr & 0xff000000) >> 24);
    printf("Port number is : %d\n", port);
}

void *handle_client(void *args)
{
    char buffer[BUFFER_SZ];
    char name[NAME_LEN];
    int leave_flag = 0;
    cli_count++;

    client_t *cli = (client_t *)args;

    // Name
    if (recv(cli->sockfd, name, NAME_LEN, 0) <= 0 || strlen(name) < 2 || strlen(name) >= NAME_LEN - 1)
    {
        printf("Enter the name correctly \n");
        leave_flag = 1;
    }
    else
    {
        strcpy(cli->name, name);
        sprintf(buffer, "%s has joined\n", cli->name);
        printf("%s", buffer);
        // send_message(buffer, cli->uid);
    }

    bzero(buffer, BUFFER_SZ);
    queue_remove(cli->uid);
    free(cli);
    cli_count--;
    pthread_detach(pthread_self());

    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Port Number not provided.Program Terminated.\n");
        printf("Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *ip = "127.0.0.1";
    int port = atoi(argv[1]);

    int option = 1;
    int listenfd = 1, connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;

    // Socket setting
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    serv_addr.sin_port = htons(port);

    // signals
    signal(SIGPIPE, SIG_IGN);

    if (setsockopt(listenfd, SOL_SOCKET, (SO_REUSEPORT | SO_REUSEADDR), (char *)&option, sizeof(option)) < 0)
    {
        printf("ERROR: setsockopt\n");
        return EXIT_FAILURE;
    }

    // Bind
    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("ERROR: bind\n");
        return EXIT_FAILURE;
    }

    // listen
    if (listen(listenfd, 10) < 0)
    {
        printf("ERROR: listen\n");
        return EXIT_FAILURE;
    }

    print_ip_addr(serv_addr, port);
    printf("=== Welcome to my server ===\n");

    while (1)
    {
        socklen_t clilen = sizeof(cli_addr);

        // accept connection from client
        connfd = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);

        // check for max-clients
        if ((cli_count + 1) == MAX_CLIENTS)
        {
            printf("Maximum clients connected. Connection Rejected: ");
            print_ip_addr(cli_addr, port);
            close(connfd);
            continue;
        }

        // Client Settings
        client_t *cli = (client_t *)malloc(sizeof(client_t));
        cli->address = cli_addr;
        cli->sockfd = connfd;
        cli->uid = uid++;

        // Add client to queue
        queue_add(cli);
        // displaying client has joined in server
        pthread_create(&tid, NULL, &handle_client, (void *)cli);

        // Reduce CPU usage
        sleep(1);
    }

    return EXIT_SUCCESS;
}