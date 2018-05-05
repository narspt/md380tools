/* 
   This is a quick test to try and call functions from the Tytera
   MD380 firmware within a 32-bit ARM Linux machine.
   
   This only runs on AARCH32 Linux, or on other Linux platforms with
   qemu-binfmt.
*/

#include<stdio.h>
#include<stdlib.h>
#include<errno.h>
#include<sys/mman.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<unistd.h>
#include<string.h>
#include<getopt.h>

#include "ambe.h"

#include <sys/select.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netdb.h>

void *firmware;
void *sram;
void *tcram;
int serverPort = 1234;

//Maps the firmware image into process memory.
void mapimage(){

  //We've already linked it, so just provide the address.
  firmware=(void*) 0x0800C000;
  sram=(void*) 0x20000000;

  int fdtcram=open("/dev/zero",0);
  //Next we need some TCRAM.  (This is hard to dump.)
  tcram=mmap((void*) 0x10000000,
	     (size_t) 0x20000,
	     PROT_READ|PROT_WRITE, //protections
	     MAP_PRIVATE,          //flags
	     fdtcram,              //file
	     0                     //offset
	     );
  

  if(firmware==(void*) -1 ||
     sram==(void*) -1 ||
     tcram==(void*) -1){
    printf("mmap() error %i.\n", errno);
    exit(1);
  }
}

/* We declare the function weak for places where it's unknown,
   as loadfirmwareversion() can't be found by symgrate. */
extern short md380_usbbuf;
#pragma weak loadfirmwareversion
void loadfirmwareversion();


//Prints the version info from the firmware.
void version(){
  /*
  //Cast the buffer and read its contents.
  short *buf=&md380_usbbuf;
  loadfirmwareversion();
  printf("Firmware Version: ");
  while(*buf)
    printf("%c",*buf++);
  printf("\n");
  */
  printf("FIXME: Version info is broken for now.");
}

//Prints usage info.
void usage(char *argv0){
  printf("Usage: %s [OPTION]\n"
	 "\t-d          Decodes AMBE\n"
	 "\t-e          Encodes AMBE\n"
	 "\t-V          Version Info\n"
	 "\t-h          Help!\n"
	 "\n"
	 "\t-v          Verbose mode.\n"
	 "\t-vv         Very verbose!\n"
	 "\n"
	 "\t-i foo      Input File\n"
	 "\t-o bar      Output File\n",
	 argv0);
}

char *infilename=NULL;
char *outfilename=NULL;
int verbosity=0;

void ambeServer(int portNumber) {
  struct sockaddr_in sa_read;
  struct hostent *hp;     /* host information */

  // open the socket
  int sockFd = socket(AF_INET, SOCK_DGRAM, 0);
  memset(&sa_read, 0x00, sizeof(struct sockaddr_in));
  sa_read.sin_family = AF_INET;
  sa_read.sin_port = htons(portNumber);
  sa_read.sin_addr.s_addr = htonl(INADDR_ANY);
  int ret = bind(sockFd, (struct sockaddr *)&sa_read, sizeof(struct sockaddr_in));

  if (verbosity)
    printf("Listening on port %d, entering while loop\n", portNumber);

  while (1) {
    unsigned char buffer[1024];

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sockFd, &fds);

    if (select(sockFd + 1, &fds, NULL, NULL, NULL) > 0) {
      if (verbosity)
        printf("data ready\n");
      socklen_t sl = sizeof(struct sockaddr_in);
      ssize_t n = recvfrom(sockFd, buffer, 1024, 0, (struct sockaddr *)&sa_read, &sl);
      if (verbosity)
        printf("got %d bytes\n", n);
      if (n==320) {
        unsigned char ambe49[7];
        memset((unsigned char *)ambe49, 0, sizeof(ambe49));
        encode_amb_buffer(ambe49, (short *)buffer);
        sendto(sockFd, ambe49, sizeof(ambe49), MSG_DONTWAIT, (struct sockaddr *)&sa_read, (ssize_t)sizeof(struct sockaddr_in));
        if (verbosity)
          printf("ambe49 sent\n");
      }
      if (n == 7) {
        short pcm[160];
        memset((unsigned char *)pcm, 0, sizeof(pcm));
        decode_amb_buffer(buffer, pcm);
        sendto(sockFd, pcm, sizeof(pcm), MSG_DONTWAIT, (struct sockaddr *)&sa_read, (ssize_t)sizeof(struct sockaddr_in));
        if (verbosity)
          printf("pcm sent\n");
      }
    }
  }
}


int main(int argc, char **argv){
  int flags, opt;
  char verb; //Main action of this run.

  verb='h';
  while((opt=getopt(argc,argv,"S:edVvo:i:"))!=-1){
    switch(opt){
      /* For any flag that sets the mode, we simply set the verb char
	 to that mode.
       */
    case 'S'://Socket
      verb=opt;
      serverPort = atoi(optarg);
      if (serverPort == 0) {
        printf("Error, must specify port\n");
        exit(1);
      }
    break;

    case 'V'://Version
    case 'd'://Decode AMBE
    case 'e'://Encode AMBE
    case 'h'://usage
      verb=opt;
      break;

      //IO filenames
    case 'o':
      outfilename=strdup(optarg);
      break;
    case 'i':
      infilename=strdup(optarg);
      break;

      //verbosity, can be applied more than once
    case 'v':
      verbosity++;
      break;
      
    default:
      printf("Unknown flag: %c\n",opt);
      //exit(1);
    }
  }

  //Do the main verb.
  switch(verb){
  case 'h'://Usage
    usage(argv[0]);
    exit(1);
    break;
  case 'd'://DECODE
    fprintf(stderr,"Decoding AMBE %s to 8kHz Raw Mono Signed %s.\n",
	    infilename?infilename:"stdin",
	    infilename?outfilename:"stdout");
    decode_amb_file(infilename,
		    outfilename);
    break;
  case 'e'://ENCODE
    fprintf(stderr,"Encoding 8kHz Raw Mono Signed %s to AMBE %s.\n",
	    infilename?infilename:"stdin",
	    infilename?outfilename:"stdout");
    encode_wav_file(infilename,
		    outfilename);
    break;
  case 'V'://Version
    version();
    break;
  case 'S':
    ambeServer(serverPort);
    break;
  default:
    printf("Usage error 2.\n");
    exit(1);
  }
  return 0;
}
