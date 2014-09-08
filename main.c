#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define CMD_MAX 5
#define MAX_BUF 1024

int verbose=0;
int debug=0;

int start_server(int port, char* root)
{
  int sockd, sockd2;
  struct sockaddr_in my_name, peer_name;
  int status, addrlen;

  /* create a socket */
  sockd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockd == -1)
  {
    perror("Socket creation error");
    return -1;
  }

  /* server address  */
  my_name.sin_family = AF_INET;
  my_name.sin_addr.s_addr = INADDR_ANY;
  my_name.sin_port = port;

  status = bind(sockd, (struct sockaddr*)&my_name, sizeof(my_name));
  if (status == -1)
  {
    perror("Binding error");
    return -1;
  }

  status = listen(sockd, 5);
  if (status == -1)
  {
    perror("Listening error");
    return -1;
  }

  for(;;)
  {
    /* wait for a connection */
    addrlen = sizeof(peer_name);
    sockd2 = accept(sockd, (struct sockaddr*)&peer_name, &addrlen);
    if (sockd2 == -1)
    {
      perror("Wrong connection");
      return -1;
    }
    /* respond with a greeting string (test functionality) */
    const char* test_string = "Hello, I am the FFTP server!\n";
    write(sockd2, test_string, strlen(test_string));
    close(sockd2);
  }
  return 0;
}

char address_buffer[FILENAME_MAX];
char* address_from_location(char* location)
{
  /* make sure location is properly terminated string */
  if(strlen(location) >= FILENAME_MAX)
  {
    return NULL;
  }
  /* copy location into address_buffer until colon is found */
  int i=0;
  while(1)
  {
    if(location[i] == ':')
    {
      address_buffer[i] = 0;
      return address_buffer;
    }
    if(location[i] == 0)
    {
      /* end of input string, indicates invalid format */
      address_buffer[0] = 0;
      return NULL;
    }
    address_buffer[i] = location[i];
    i++;
  }
}

int execute_cmd(char* cmd, char* location, int port)
{
  int sockd;
  int count;
  struct sockaddr_in serv_name;
  char buf[MAX_BUF];
  int status;

  /* create a socket */
  sockd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockd == -1)
  {
    perror("Socket creation");
    return -1;
  }

  /* server address */
  serv_name.sin_family = AF_INET;
  inet_aton(address_from_location(location), &serv_name.sin_addr);
  serv_name.sin_port = port;

  /* connect to the server */
  status = connect(sockd, (struct sockaddr*)&serv_name, sizeof(serv_name));
  if (status == -1)
  {
    perror("Connection error");
    return -1;
  }

  count = read(sockd, buf, MAX_BUF);
  write(1, buf, count);

  close(sockd);
  return 0;
}




int main(int argc, char **argv)
{
  int c;
  char cmd[CMD_MAX] = "";
  char location[FILENAME_MAX] = "";
  char root_location[FILENAME_MAX] = "";
  int server=0;
  int port = 6789;


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
    if(optind != argc - 2)
    {
      printf("Error, parameter missing\n");
      print_usage();
      return -1;
    }

    strncpy(cmd, argv[optind], CMD_MAX);
    strncpy(location, argv[optind+1], FILENAME_MAX);

    if(debug)
    {
      printf("Executing client command:\n");
      printf("Port: %d\n", port);
      printf("Verbose: %d\n", verbose);
      printf("Cmd: %s\n", cmd);
      printf("Location: %s\n", location);
    }

    return execute_cmd(cmd, location, port);
  }
}

int print_usage()
{
  printf("Usage: ft <options> CMD HOST:PATH\n");
}
