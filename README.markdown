This project is an attempt to create a tftp-like client and
server application for transferring files over a network. 

TFtp: http://en.wikipedia.org/wiki/Trivial_File_Transfer_Protocol

The application should have a good verbose output and enough debug
information to really see what is going on.

Requirements
============
- Implement a file transfer protocol with BSD-like sockets.
- Use ANSI-C as the implementation language.
- The protocol must consists of two commands:
   - GET hostname:filename [filename]
   - PUT filename hostname[:filename]
  The filename is the complete path and name of the file.
- Protocol defined must be easily extendable also to other
  commands, for example DELETE to delete files on the other
  end.
- Implement a client that uses this protocol and connects to
  port 6789 on the server without user authentication. Port
  number must be changeable.
- Implement a server that listens for connections on port 6789.
  Port number must be changeable.
- The server must only allow access to a single specific
  directory and its subdirectories ("virtual document root").
- Concentrate on providing good logging capability and robust,
  secure applications not overwhelmed with extra features. Remember
  correct and efficient commenting of the source code.
- The source code should compile on as many platforms as
  possible with an ANSI-C compiler (full warnings on,
  system specific extensions, use for example
  gcc -Wall -ansi -pedantic).


Example of use

% ft GET ft.host.net:/tmp/data.txt
...output from operation...

% ft PUT data.txt ft.host.net:/tmp/data.txt
...output from operation...

