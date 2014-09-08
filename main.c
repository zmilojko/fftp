#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>

#define CMD_MAX 10
#define BUF_SIZE 8192

#define CMD_PUT 1
#define CMD_GET 2

#define vprintf(...) if(debug) { printf(__VA_ARGS__); }
#define dprintf(...) if(debug) { printf(__VA_ARGS__); }

int verbose=0;
int debug=0;

// Following four are the actual command handlers, two on each side.
// Both assume that the socket is properly open.
int server_send_file(int socket, const char* filepath)
{
    char buffer[BUF_SIZE];

    dprintf("opening file %s\n", filepath);
    FILE *f = fopen(filepath, "r");
    if(f == NULL)
    {
        printf("Cannot open file for reading.\n");
        return -1;
    }
    dprintf("file open, now sendingi\n");
    while(!feof(f))
    {
        int count, status;

        count = fread(buffer, 1, BUF_SIZE, f);
        dprintf("sending %d bytes\n", count);
        status = send(socket, buffer, count, 0);
        if(status < 0)
        {
            printf("Cannot send file content.");
            return -1;
        }
    }
    dprintf("file sending complete\n");
    fclose(f);
    return 0;
}
int server_receive_file(int socket, char* prev_buffer, int prev_buffer_length, const char* filepath)
{
    char buffer[BUF_SIZE];
    int count;

    FILE *f = fopen(filepath, "w");
    if(f == NULL)
    {
        printf("Cannot open file for reading.\n");
        return -1;
    }

    /* first write the part that might have been received with the handshake */
    dprintf("writing %d bytes from previous buffer\n", prev_buffer_length);
    if(fwrite(prev_buffer, 1, prev_buffer_length, f) != prev_buffer_length)
    {
        printf("Cannot write to file.\n");
        return -1;
    }

    while ((count = read(socket, buffer, BUF_SIZE))>0)
    {
        dprintf("received %d bytes", count);
        fwrite(buffer, 1, count, f);
    }

    fclose(f);
    return 0;
}

int client_send_file(int socket, const char* filepath)
{
    char buffer[BUF_SIZE];

    FILE *f = fopen(filepath, "r");
    if(f == NULL)
    {
        printf("Cannot open file for reading.\n");
        return -1;
    }
    dprintf("file open, now sending\n");
    while(!feof(f))
    {
        int count, status;

        count = fread(buffer, 1, BUF_SIZE, f);
        dprintf("sending %d bytes\n", count);
        status = send(socket, buffer, count, 0);
        if(status < 0)
        {
            printf("Cannot send file content.");
            return -1;
        }
    }
    dprintf("file sending complete\n");
    fclose(f);
    return 0;


}
/* Note that this GET handler actually prints to stdout, as it was
   not specified anywhere to which file to write to. */
int client_receive(int socket)
{
    char buffer[BUF_SIZE];
    int count;
    while ((count = read(socket, buffer, BUF_SIZE))>0)
    {
        printf("%s", buffer);
    }
}

char address_buffer[FILENAME_MAX];
char* address_from_location(const char* location)
{
    const char* ploc = strchr(location, ':');
    if(ploc == NULL)
    {
        return NULL;
    }

    return strncpy(address_buffer, location, ploc - location);
}

const char* path_from_location(const char* location)
{
    const char* ploc = strchr(location, ':');
    if(ploc == NULL)
    {
        return NULL;
    }
    else
    {
        return ploc+1;
    }
}

int start_server(int port, char* root)
{
    vprintf("Starting server on port %d with virtual root directory %s\n", port, root);
    int listen_socket, recv_socket;
    struct sockaddr_in my_name, peer_name;
    int status, addrlen;

    /* create a socket */
    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == -1)
    {
        perror("error creating socket");
        return -1;
    }
    /* server address  */
    my_name.sin_family = AF_INET;
    my_name.sin_addr.s_addr = INADDR_ANY;
    my_name.sin_port = htons(port);
    status = bind(listen_socket, (struct sockaddr*)&my_name, sizeof(my_name));
    if (status == -1)
    {
        perror("Binding error");
        return -1;
    }
    status = listen(listen_socket, 5);
    if (status == -1)
    {
        perror("Listening error");
        return -1;
    }

    dprintf("Succesfully started server, waiting for clients\n");

    while(1)
    {
        char buffer[BUF_SIZE];
        int i = 0;
        int cmd;
        char location[FILENAME_MAX];
        char* location_start;
        const char* path;

        /* wait for a connection */
        addrlen = sizeof(peer_name);
        dprintf("\n\n\nWaiting for an incoming request...\n");
        /* following fflush is neccessary as sockets seem to block the stdout, so the printf have
           no effect. While this is a performance issue, performance is not an issue. */
        fflush(stdout);
        recv_socket = accept(listen_socket, (struct sockaddr*)&peer_name, &addrlen);
        if (recv_socket == -1)
        {
            perror("Wrong connection");
            return -1;
        }

        dprintf("Received new connection, trying to read command and location\n");
        /* try to read a command and location (both zero terminated
           but arriving together), concatinate until you find both
           zero terminators. */
        memset(buffer, 255, BUF_SIZE);
        buffer[BUF_SIZE - 1] = 0;
        cmd=0;
        while(1)
        {
            int count = recv(recv_socket, buffer + i, BUF_SIZE - i, 0);

            if(count == -1)
            {
                perror("error reading socket");
                return -1;
            }
            if(count == 0)
            {
                printf("Received end of sending when not expected, closing connection and hoping for a better client\n");
                break;
            }
            i += count;
            dprintf("buffer is %s\n", buffer);
            if(strlen(buffer) < CMD_MAX)
            {
                if(!strcmp(buffer, "PUT"))
                {
                    cmd = CMD_PUT;
                }
                else if(!strcmp(buffer, "GET"))
                {
                    cmd = CMD_GET;
                }
                else
                {
                    printf("Illegal command received from client\n");
                    break;
                }
                dprintf("Command identified with ID: %d, trying to get location\n", cmd);
                location_start = buffer + strlen(buffer) + 1;
                if(strlen(location_start) < FILENAME_MAX)
                {

                    dprintf("about to copy %s\n", location_start);
                    strcpy(location, location_start);

                    //check here if location is allowed
                    path = path_from_location(location);
                    if(!(strstr(path, root) == path))
                    {
                        cmd = 0;
                        vprintf("Illegal path, not in the root.");
                    }

                    break;
                }
                else
                {
                    cmd = 0;
                }
            }
        }
        printf("Received command %d with location %s. Executing.\n", cmd, path);

        switch(cmd)
        {
        case CMD_PUT:
            server_receive_file(recv_socket, location_start + strlen(location_start) + 1, buffer + i - location_start - strlen(location_start) - 1, path);
            break;
        case CMD_GET:
            server_send_file(recv_socket, path);
            break;
        }

        close(recv_socket);
        //printf("Received command: %s and location: %s", buffer, buffer /*location*/ );
        /* respond with a greeting string (test functionality) */
    }
    return 0;
}


int execute_cmd(char* cmd, char* location, int port, const char* source_location)
{
    int res = -1;

    printf("executing command %s %s %d\n", cmd, location, port);

    printf("Address: %s, filepath: %s\n", address_from_location(location), path_from_location(location));
    int sockd;
    int count;
    struct sockaddr_in serv_name;
    char buf[BUF_SIZE];
    int status;

    /* create a socket */
    sockd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockd == -1)
    {
        perror("Socket creation");
        return -1;
    }
    /* server address */
    serv_name.sin_family = AF_INET;
    printf("Opening address to %s", address_from_location(location));

    serv_name.sin_addr.s_addr = inet_addr(address_from_location(location));
    serv_name.sin_port = htons(port);

    /* connect to the server */
    status = connect(sockd, (struct sockaddr*)&serv_name, sizeof(serv_name));
    if (status == -1)
    {
        perror("Connection error");
        return -1;
    }

    printf("sending: %s", cmd);
    status = send(sockd, cmd, strlen(cmd)+1, 0);
    if (status < 0)
    {
        printf("error sending %d", errno);
    }
    printf("sending: %s", location);
    status = send(sockd, location, strlen(location)+1, 0);
    if (status < 0)
    {
        printf("error sending %d", errno);
    }

    if(!strncmp(cmd, "GET", 3))
    {
        vprintf("Receiving the file from the server.\n");
        res = client_receive(sockd);
    }
    else if(!strncmp(cmd, "PUT", 3))
    {
        vprintf("Sending the file to the server.\n");
        res = client_send_file(sockd, source_location);
    }
    close(sockd);
    return res;
}

int main(int argc, char **argv)
{
    int c;
    char cmd[CMD_MAX] = "";
    char location[FILENAME_MAX] = "";
    char root_location[FILENAME_MAX] = "";
    int server=0;
    int port = 6789;
    char source_location[FILENAME_MAX];


    while(1)
    {
        static struct option long_options[] =
        {
            {"verbose", no_argument, 0, 'v' },
            {0,0,0,0}
        };
        int option_index;
        c = getopt_long (argc, argv, "vsp:r:d",
                         long_options, &option_index);
        if(c==-1)
        {
            /* this indicates we are done processing options */
            break;
        }
        switch(c)
        {
        case 'p':
            port = atoi(optarg);
            if(port <= 0)
            {
                printf("Illegal port number.\n");
                print_usage();
                return -1;
            }
        case 's':
            server = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'r':
            /* root location, for server only */
            strncpy(root_location, optarg, FILENAME_MAX);
            break;
        case 'd':
            debug=1;
            break;
        default:
            print_usage();
            return -1;
        }
    }

    if(server)
    {
        /* If option 's' is specified, this is a server. Server must be
           started with the 'r option specified. */
        if(strlen(root_location) == 0)
        {
            printf("Error, option s (Server) must be given with option r (root_location)\n");
            print_usage();
            return -1;
        }

        if(optind != argc)
        {
            print_usage();
            return -1;
        }

        if(debug)
        {
            printf("Starting server with following options:\n");
            printf("Port: %d\n", port);
            printf("Server: %d\n", server);
            printf("Verbose: %d\n", verbose);
            printf("Root: %s\n", root_location);
        }
        return start_server(port, root_location);
    }
    else
    {
        /* If option 's' is not specified, this is a client. Retrieve
           the two compulsory arguments: CMD and PATH. */
        if(optind >= argc - 2)
        {
            printf("Error, parameter missing\n");
            print_usage();
            return -1;
        }

        strncpy(cmd, argv[optind], CMD_MAX);
        strncpy(location, argv[optind+1], FILENAME_MAX);

        if(!strcmp(cmd, "PUT"))
        {
            if(optind != argc - 3)
            {
                printf("Error, parameter missing\n");
                print_usage();
                return -1;
            }
            strncpy(source_location, argv[optind+1], FILENAME_MAX);
            strncpy(location, argv[optind+2], FILENAME_MAX);
        }
        else
        {
            if(optind != argc - 2)
            {
                printf("Error, parameter missing\n");
                print_usage();
                return -1;
            }
            strncpy(location, argv[optind+1], FILENAME_MAX);
        }

        /* check that address:filepath is legit */
        if(address_from_location(location) == NULL)
        {
            printf("Location must be in form example.net:/tmp/filepath\n");
            print_usage();
            return -1;
        }

        if(debug)
        {
            printf("Executing client command:\n");
            printf("Port: %d\n", port);
            printf("Verbose: %d\n", verbose);
            printf("Cmd: %s\n", cmd);
            printf("Location: %s\n", location);
        }
        return execute_cmd(cmd, location, port, source_location);
    }
}

int print_usage()
{
    printf("Usage: ft <options> CMD HOST:PATH\n");
}
