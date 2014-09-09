#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <errno.h>

#define CMD_MAX 10
#define BUF_SIZE 8192

#define CMD_PUT 1
#define CMD_GET 2

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvariadic-macros"

#define vprintf(...) if(debug) { printf(__VA_ARGS__); }
#define dprintf(...) if(debug) { printf(__VA_ARGS__); }

#pragma GCC diagnostic pop

/* Build instructions: to build on Linux and Mac (tested tried on Mac yet),
   following CMakeList.txt file can do the magic:

       cmake_minimum_required(VERSION 2.8.7)
       project(FFtp)
       add_executable(ft main.c)

   If CMake and standard build tools and libraries are installed,
   you should be able to build with:

       cmake .
       make

   To test, try for example the following in two separate consoles:

       server> echo "content of a server-side file" > /home/myself/ftroot/sfile
       server> ft -sr /home/myself/ftroot
       (server should hang here)

       client> ft GET localhost:/home/myself/ftroot/sfile
       client> echo "content of a client-side file" > /some/location/cfile
       client> ft PUT /some/location/cfile localhost:/home/myself/ftroot/cf

*/

int verbose=0;
int debug=0;

/* Following three are the actual command handlers, two on each side.
   All three assume that the socket is properly open and they leave it
   open.

   Note that server and client side sending are almost
   identical and same function can be used.

   Note that file operations are not checked for errors (such as corrupt
   disk or file system), that should as well be done for product grade.

   None of the functions verifies in any way if the file has been received
   completelly and without errors. There is no post-transfer handshake. */
int send_file(int socket, const char* filepath)
{
    char buffer[BUF_SIZE];
    FILE *f;

    dprintf("opening file %s\n", filepath);
    f = fopen(filepath, "r");
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

/* This function receives the file from the socket and writes it into the newly
   created file. Note that some of the file content might have been received
   with the handshake message, and is passed to this function in a separate
   buffer (see comment where this function is called). That data is written to
   the file first, and then data is read from the socket and written in the
   file until the socket is closed. */
int server_receive_file(int socket, char* prev_buffer, int prev_buffer_length,
                        const char* filepath)
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

    while ((count = recv(socket, buffer, BUF_SIZE, 0))>0)
    {
        dprintf("received %d bytes", count);
        fwrite(buffer, 1, count, f);
    }

    if(count < 0)
    {
        perror("Error receiving file content.\n");
        return -1;
    }

    fclose(f);
    return 0;
}

/* Note that this GET handler actually prints to stdout, as it was
   not specified anywhere to which file to write to.

   This is a messy solution as it is difficult to distinguish between
   the transfer content and the debug/trace messages.*/
int client_receive(int socket)
{
    char buffer[BUF_SIZE];
    int count;
    while ((count = recv(socket, buffer, BUF_SIZE, 0))>0)
    {
        printf("%s", buffer);
    }
    if(count < 0)
    {
        perror("Error receiving file content.\n");
        return -1;
    }
    return 0;
}

/* Following two functions parse location to get web address (IP
   is accepted only, not an arbitrary hostname) and the file path.

   Location: 127.0.0.1:/path/to/file
   Address: 127.0.0.1
   Path: /path/to/file

   Both functions return NULL in case of illegal location string. */
char address_buffer[FILENAME_MAX];
const char* address_from_location(const char* location)
{
    const char* ploc = strchr(location, ':');
    if(ploc == NULL)
    {
        return NULL;
    }

    return (const char*) strncpy(address_buffer, location, ploc - location);
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

/* Main server handler. This function never returns, unless there
   is an error. */
int start_server(int port, char* root)
{
    int listen_socket, recv_socket;
    struct sockaddr_in my_name, peer_name;
    int status;
    unsigned int addrlen;

    vprintf("Starting server on port %d with virtual root directory %s\n",
            port, root);

    /* create a listening socket */
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
        int res = -1;

        /* wait for a connection */
        addrlen = sizeof(peer_name);
        dprintf("\n\n\nWaiting for an incoming request...\n");
        /* following fflush is neccessary as sockets seem to block the stdout,
           so the printf have no effect. While this is a performance issue,
           performance is not an issue. */
        fflush(stdout);

        /* Block here until a connection request has been received. Once you
        have received a connection, handle it and wait for/handle another
        connection. */
        recv_socket = accept(listen_socket,
                             (struct sockaddr*)&peer_name, &addrlen);
        if (recv_socket == -1)
        {
            perror("Wrong connection");
            return -1;
        }

        dprintf("Received new connection, trying to read cmd and location\n");
        /* try to read a command and location (both zero terminated
           but arriving together), concatinate until you find both
           zero terminators.

           To be able to properly spot the zero-terminators, fill the buffer
           with something else (255) but properly terminate it not to allow
           strlen to cause heavoc beyond the buffer. This way
           strlen(buffer) < CMD_MAX will indicate a zero terminator has been
           read. */
        memset(buffer, 255, BUF_SIZE);
        buffer[BUF_SIZE - 1] = 0;
        cmd=0;
        /* Loop until you have received a full handshake (cmd+location) or
           and error occurs. */
        while(1)
        {
            /* Receive data into a buffer offseted by i, which is the count of
            the previously received data. */
            int count = recv(recv_socket, buffer + i, BUF_SIZE - i, 0);

            if(count == -1)
            {
                perror("error reading socket");
                return -1;
            }
            if(count == 0)
            {
                printf("Received end of sending when not expected, closing "
                       "connection and hoping for a better client\n");
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
                dprintf("Command identified with ID: %d,"
                        " trying to get location\n", cmd);

                /* After the command has been received, try to find the second
                   zero-terminator, which ends the location. */
                location_start = buffer + strlen(buffer) + 1;
                if(strlen(location_start) < FILENAME_MAX)
                {

                    dprintf("about to copy %s\n", location_start);
                    strcpy(location, location_start);

                    /*check here if location is allowed */
                    path = path_from_location(location);
                    if(!(strstr(path, root) == path))
                    {
                        cmd = 0;
                        vprintf("Illegal path, not in the root.");
                        break;
                    }

                    break;
                }
                else
                {
                    cmd = 0;
                }
            }
        }
        vprintf("Received command %d with location %s. Executing.\n", cmd, path);

        /* If we broke from the preious loop with command set to something,
           execute that command. If cmd is zero, but we are out of the previous
           loop, it indicates and error, simply close the socket and take
           the next connection. */
        switch(cmd)
        {
        case CMD_PUT:
            /* The buffer might contain data that are part of the sent file,
               which would be after the second terminator. Following nasty
               pointer arithmetic gives us the offset and length of that data
               in the buffer. */
            res = server_receive_file(recv_socket,
                                      location_start + strlen(location_start) + 1,
                                      buffer + i - location_start - strlen(location_start) - 1,
                                      path);
            break;
        case CMD_GET:
            res = send_file(recv_socket, path);
            break;
        }

        close(recv_socket);

        if(res == 0)
        {
            vprintf("Completed OK.");
        }
        else
        {
            vprintf("Error(s) occured.");
        }
    }

    /* Following is never executed, and the outer loop cannot be broken from. */
    return -1;
}

/* Main client handler. Send the handshake message and call the appropriate
   send or receive handler. */
int execute_cmd(char* cmd, char* location, int port,
                const char* source_location)
{
    int res = -1;
    int sockd;
    struct sockaddr_in serv_name;
    int status;

    vprintf("executing command %s %s %d\n", cmd, location, port);

    /* create a socket */
    sockd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockd == -1)
    {
        perror("Socket creation");
        return -1;
    }
    /* server address */
    serv_name.sin_family = AF_INET;
    vprintf("Opening address to %s\n", address_from_location(location));

    serv_name.sin_addr.s_addr = inet_addr(address_from_location(location));
    serv_name.sin_port = htons(port);

    /* connect to the server */
    status = connect(sockd, (struct sockaddr*)&serv_name, sizeof(serv_name));
    if (status == -1)
    {
        perror("Connection error");
        return -1;
    }

    vprintf("sending cmd: %s\n", cmd);
    status = send(sockd, cmd, strlen(cmd)+1, 0);
    if (status < 0)
    {
        perror("error sending %d");
    }
    vprintf("sending location: %s\n", location);
    status = send(sockd, location, strlen(location)+1, 0);
    if (status < 0)
    {
        perror("error sending %d");
    }

    if(!strncmp(cmd, "GET", 3))
    {
        vprintf("Receiving the file from the server.\n");
        res = client_receive(sockd);
    }
    else if(!strncmp(cmd, "PUT", 3))
    {
        vprintf("Sending the file to the server.\n");
        res = send_file(sockd, source_location);
    }
    close(sockd);
    return res;
}

/* Inline command line instructions. Expand the output from this
   function to make it easier for user to understan how to use the
   application. Add a link to where more info can be found.

   For the purpose of this exceprsize, just demonstrating how this
   can be done. */
int print_usage()
{
    printf("Usage: ft <options> CMD HOST:PATH\n");
    printf("Much more instructions should be added here...\n");
    return 0;
}

/* Main fucntion: parse parameter and call one of the
   top level handlers (Server or client). */
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
            debug = 1;
            verbose = 1;
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
            printf("Error, option -s (Server) must be given "
                   "with option r (root_location)\n");
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
        if(optind > argc - 2)
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
            printf("Location must be in form example.net:/path/to/file\n");
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

