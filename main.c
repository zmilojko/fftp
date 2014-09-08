#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#define CMD_MAX 5

int verbose=0;
int debug=0;

int start_server(int port, char* root)
{
}

int execute_cmd(char* cmd, char* location, int port)
{
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
