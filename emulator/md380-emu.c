/* 
   This is a quick test to try and call functions from the Tytera
   MD380 firmware within a 32-bit ARM Linux machine.
   
   This only runs on AARCH32 Linux, or on other Linux platforms with
   qemu-binfmt.
   
   Parts of code on this file adapted from other opensource projects:
    - mbelib (only Golay check/correct code): https://github.com/szechyjs/mbelib
    - dmr_utils: https://github.com/n0mjs710/dmr_utils
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

#include <stdbool.h>
#include <math.h>

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
	 "\t-S port     AMBEServer\n"
	 "\n"
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






void dump(const unsigned char *data, unsigned int length) {
  unsigned int offset = 0U;

  while (length > 0U) {
    unsigned int bytes = (length > 16U) ? 16U : length;

    printf("%04X:  ", offset);

    for (unsigned int i = 0U; i < bytes; i++)
      printf("%02X ", data[offset + i]);

    for (unsigned int i = bytes; i < 16U; i++)
      printf("   ");

    printf("   *");
    for (unsigned int i = 0U; i < bytes; i++) {
      unsigned char c = data[offset + i];
      if ((c >= 32) && (c <= 126))
        printf("%c", c);
      else
        printf(".");
    }
    printf("*\n");

    offset += 16U;

    if (length >= 16U)
      length -= 16U;
    else
      length = 0U;
  }
}

unsigned char bits_to_byte(bool bits[8]) {
  unsigned char byte = 0;
  if (bits[0]) byte |= 128;
  if (bits[1]) byte |= 64;
  if (bits[2]) byte |= 32;
  if (bits[3]) byte |= 16;
  if (bits[4]) byte |= 8;
  if (bits[5]) byte |= 4;
  if (bits[6]) byte |= 2;
  if (bits[7]) byte |= 1;
  return byte;
}

void bits_to_bytes(bool *bits, unsigned char *bytes, unsigned int length) {
  for (unsigned int i = 0; i < length; i++)
    bytes[i] = bits_to_byte(&bits[i * 8]);
} 

void byte_to_bits(unsigned char byte, bool bits[8]) {
  bits[0] = (byte & 128) > 0;
  bits[1] = (byte & 64) > 0;
  bits[2] = (byte & 32) > 0;
  bits[3] = (byte & 16) > 0;
  bits[4] = (byte & 8) > 0;
  bits[5] = (byte & 4) > 0;
  bits[6] = (byte & 2) > 0;
  bits[7] = (byte & 1) > 0;
}

void bytes_to_bits(unsigned char *bytes, bool *bits, unsigned int length) {
  for (unsigned int i = 0; i < length; i++)
    byte_to_bits(bytes[i], &bits[i * 8]);
}






const int golayGenerator[12] = {
  0x63a, 0x31d, 0x7b4, 0x3da, 0x1ed, 0x6cc, 0x366, 0x1b3, 0x6e3, 0x54b, 0x49f, 0x475
};

const int golayMatrix[2048] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 72, 0, 0, 0, 0, 0, 0, 0, 2084, 0, 0, 0, 769, 0, 1024, 144,
  2, 0, 0, 0, 0, 0, 0, 0, 72, 0, 0, 0, 72, 0, 72, 72, 72, 0, 0, 0, 16, 0, 1, 1538, 384, 0, 134, 2048, 1056, 288,
  2576, 5, 72, 0, 0, 0, 0, 0, 0, 0, 1280, 0, 0, 0, 4, 0, 546, 144, 2049, 0, 0, 0, 66, 0, 1, 144, 520, 0, 2056, 144,
  1056, 144, 324, 144, 144, 0, 0, 0, 2688, 0, 1, 32, 22, 0, 272, 3, 1056, 3076, 128, 768, 72, 0, 1, 268, 1056, 1, 1, 2112, 1, 576,
  1056, 1056, 1056, 10, 1, 144, 1056, 0, 0, 0, 0, 0, 0, 0, 1280, 0, 0, 0, 160, 0, 21, 2560, 2, 0, 0, 0, 16, 0, 704, 9,
  2, 0, 2056, 1092, 2, 288, 2, 2, 2, 0, 0, 0, 16, 0, 2050, 132, 545, 0, 1536, 3, 2308, 288, 128, 1040, 72, 0, 16, 16, 16, 288,
  1036, 2112, 16, 288, 65, 648, 16, 288, 288, 288, 2, 0, 0, 0, 1280, 0, 1280, 1280, 1280, 0, 2056, 3, 592, 64, 128, 44, 1280, 0, 2056, 544,
  133, 6, 48, 2112, 1280, 2056, 2056, 256, 2056, 1537, 2056, 144, 2, 0, 100, 3, 8, 536, 128, 2112, 1280, 3, 128, 3, 3, 128, 128, 3, 128, 1152,
  770, 2112, 16, 2112, 1, 2112, 2112, 20, 2056, 3, 1056, 288, 128, 2112, 516, 0, 0, 0, 0, 0, 0, 0, 131, 0, 0, 0, 4, 0, 1024, 2560,
  304, 0, 0, 0, 16, 0, 1024, 320, 520, 0, 1024, 42, 2240, 1024, 1024, 5, 1024, 0, 0, 0, 16, 0, 772, 32, 3072, 0, 2081, 1408, 514, 18,
  128, 5, 72, 0, 16, 16, 16, 2184, 98, 5, 16, 576, 264, 5, 16, 5, 1024, 5, 5, 0, 0, 0, 4, 0, 2128, 32, 520, 0, 4, 4,
  4, 265, 128, 1090, 4, 0, 416, 3073, 520, 6, 520, 520, 520, 576, 19, 256, 4, 2080, 1024, 144, 520, 0, 1034, 32, 321, 32, 128, 32, 32, 576,
  128, 2072, 4, 128, 128, 32, 128, 576, 2052, 130, 16, 1296, 1, 32, 520, 576, 576, 576, 1056, 576, 128, 5, 2306, 0, 0, 0, 16, 0, 40, 2560,
  68, 0, 322, 2560, 1033, 2560, 128, 2560, 2560, 0, 16, 16, 16, 6, 2305, 1184, 16, 129, 548, 256, 16, 88, 1024, 2560, 2, 0, 16, 16, 16, 1089,
  128, 266, 16, 12, 128, 96, 16, 128, 128, 2560, 128, 16, 16, 16, 16, 512, 16, 16, 16, 3074, 16, 16, 16, 288, 128, 5, 16, 0, 513, 200,
  2082, 6, 128, 17, 1280, 1072, 128, 256, 4, 128, 128, 2560, 128, 6, 1088, 256, 16, 6, 6, 6, 520, 256, 2056, 256, 256, 6, 128, 256, 97, 2304,
  128, 1540, 16, 128, 128, 32, 128, 128, 128, 3, 128, 128, 128, 128, 128, 41, 16, 16, 16, 6, 128, 2112, 16, 576, 128, 256, 16, 128, 128, 1032,
  128, 0, 0, 0, 0, 0, 0, 0, 528, 0, 0, 0, 160, 0, 1024, 262, 2049, 0, 0, 0, 66, 0, 1024, 9, 384, 0, 1024, 2048, 28, 1024,
  1024, 608, 1024, 0, 0, 0, 1029, 0, 2050, 32, 384, 0, 272, 2048, 514, 641, 36, 1040, 72, 0, 552, 2048, 384, 84, 384, 384, 384, 2048, 65, 2048,
  2048, 10, 1024, 2048, 384, 0, 0, 0, 66, 0, 140, 32, 2049, 0, 272, 1544, 2049, 64, 2049, 2049, 2049, 0, 66, 66, 66, 2816, 48, 1028, 66, 37,
  640, 256, 66, 10, 1024, 144, 2049, 0, 272, 32, 8, 32, 1600, 32, 32, 272, 272, 196, 272, 10, 272, 32, 2049, 1152, 2052, 529, 66, 10, 1, 32,
  384, 10, 272, 2048, 1056, 10, 10, 10, 516, 0, 0, 0, 160, 0, 2050, 9, 68, 0, 160, 160, 160, 64, 776, 1040, 160, 0, 260, 9, 3584, 9,
  48, 9, 9, 530, 65, 256, 160, 2180, 1024, 9, 2, 0, 2050, 832, 8, 2050, 2050, 1040, 2050, 12, 65, 1040, 160, 1040, 2050, 1040, 1040, 1152, 65, 38,
  16, 512, 2050, 9, 384, 65, 65, 2048, 65, 288, 65, 1040, 516, 0, 513, 2068, 8, 64, 48, 642, 1280, 64, 1030, 256, 160, 64, 64, 64, 2049, 1152,
  48, 256, 66, 48, 48, 9, 48, 256, 2056, 256, 256, 64, 48, 256, 516, 1152, 8, 8, 8, 261, 2050, 32, 8, 2592, 272, 3, 8, 64, 128, 1040,
  516, 1152, 1152, 1152, 8, 1152, 48, 2112, 516, 1152, 65, 256, 516, 10, 516, 516, 516, 0, 0, 0, 2312, 0, 1024, 32, 68, 0, 1024, 81, 514, 1024,
  1024, 136, 1024, 0, 1024, 644, 33, 1024, 1024, 2066, 1024, 1024, 1024, 256, 1024, 1024, 1024, 1024, 1024, 0, 192, 32, 514, 32, 25, 32, 32, 12, 514, 514,
  514, 2368, 1024, 32, 514, 259, 2052, 1096, 16, 512, 1024, 32, 384, 176, 1024, 2048, 514, 1024, 1024, 5, 1024, 0, 513, 32, 1168, 32, 258, 32, 32, 2178,
  104, 256, 4, 532, 1024, 32, 2049, 24, 2052, 256, 66, 193, 1024, 32, 520, 256, 1024, 256, 256, 1024, 1024, 256, 1024, 32, 2052, 32, 32, 32, 32, 32,
  32, 1025, 272, 32, 514, 32, 128, 32, 32, 2052, 2052, 32, 2052, 32, 2052, 32, 32, 576, 2052, 256, 137, 10, 1024, 32, 80, 0, 513, 1026, 68, 400,
  68, 68, 68, 12, 2064, 256, 160, 35, 1024, 2560, 68, 2144, 138, 256, 16, 512, 1024, 9, 68, 256, 1024, 256, 256, 1024, 1024, 256, 1024, 12, 1312, 2177,
  16, 512, 2050, 32, 68, 12, 12, 12, 514, 12, 128, 1040, 257, 512, 16, 16, 16, 512, 512, 512, 16, 12, 65, 256, 16, 512, 1024, 194, 2088, 513,
  513, 256, 513, 3080, 513, 32, 68, 256, 513, 256, 256, 64, 128, 256, 26, 256, 513, 256, 256, 6, 48, 256, 2176, 256, 256, 256, 256, 256, 1024, 256,
  256, 82, 513, 32, 8, 32, 128, 32, 32, 12, 128, 256, 3136, 128, 128, 32, 128, 1152, 2052, 256, 16, 512, 328, 32, 1027, 256, 34, 256, 256, 2065,
  128, 256, 516, 0, 0, 0, 0, 0, 0, 0, 528, 0, 0, 0, 4, 0, 2432, 1057, 2, 0, 0, 0, 1160, 0, 1, 320, 2, 0, 112, 2048,
  2, 524, 2, 2, 2, 0, 0, 0, 290, 0, 1, 132, 3072, 0, 1536, 2048, 145, 18, 36, 768, 72, 0, 1, 2048, 580, 1, 1, 56, 1, 2048,
  264, 2048, 2048, 1216, 1, 2048, 2, 0, 0, 0, 4, 0, 1, 2058, 224, 0, 4, 4, 4, 64, 1048, 768, 4, 0, 1, 544, 2320, 1, 1, 1028,
  1, 1282, 640, 73, 4, 2080, 1, 144, 2, 0, 1, 1104, 8, 1, 1, 768, 1, 168, 2114, 768, 4, 768, 1, 768, 768, 1, 1, 130, 1, 1,
  1, 1, 1, 20, 1, 2048, 1056, 1, 1, 768, 1, 0, 0, 0, 2113, 0, 40, 132, 2, 0, 1536, 280, 2, 64, 2, 2, 2, 0, 260, 544,
  2, 3088, 2, 2, 2, 129, 2, 2, 2, 2, 2, 2, 2, 0, 1536, 132, 8, 132, 336, 132, 132, 1536, 1536, 96, 1536, 2057, 1536, 132, 2, 74,
  2208, 1281, 16, 512, 1, 132, 2, 20, 1536, 2048, 2, 288, 2, 2, 2, 0, 146, 544, 8, 64, 2564, 17, 1280, 64, 289, 3200, 4, 64, 64, 64,
  2, 544, 1088, 544, 544, 392, 1, 544, 2, 20, 2056, 544, 2, 64, 2, 2, 2, 2304, 8, 8, 8, 1058, 1, 132, 8, 20, 1536, 3, 8, 64,
  128, 768, 2096, 20, 1, 544, 8, 1, 1, 2112, 1, 20, 20, 20, 448, 20, 1, 1032, 2, 0, 0, 0, 4, 0, 40, 320, 3072, 0, 4, 4,
  4, 18, 577, 136, 4, 0, 2562, 320, 33, 320, 148, 320, 320, 129, 264, 1552, 4, 2080, 1024, 320, 2, 0, 192, 521, 3072, 18, 3072, 3072, 3072, 18,
  264, 96, 4, 18, 18, 18, 3072, 1060, 264, 130, 16, 512, 1, 320, 3072, 264, 264, 2048, 264, 18, 264, 5, 672, 0, 4, 4, 4, 1664, 258, 17,
  4, 4, 4, 4, 4, 2080, 4, 4, 4, 24, 1088, 130, 4, 2080, 1, 320, 520, 2080, 4, 4, 4, 2080, 2080, 2080, 4, 2304, 560, 130, 4, 76,
  1, 32, 3072, 1025, 4, 4, 4, 18, 128, 768, 4, 130, 1, 130, 130, 1, 1, 130, 1, 576, 264, 130, 4, 2080, 1, 1032, 80, 0, 40, 1026,
  896, 40, 40, 17, 40, 129, 2064, 96, 4, 1284, 40, 2560, 2, 129, 1088, 2060, 16, 512, 40, 320, 2, 129, 129, 129, 2, 129, 2, 2, 2, 2304,
  7, 96, 16, 512, 40, 132, 3072, 96, 1536, 96, 96, 18, 128, 96, 257, 512, 16, 16, 16, 512, 512, 512, 16, 129, 264, 96, 16, 512, 2116, 1032,
  2, 2304, 1088, 17, 4, 17, 40, 17, 17, 522, 4, 4, 4, 64, 128, 17, 4, 1088, 1088, 544, 1088, 6, 1088, 17, 2176, 129, 1088, 256, 4, 2080,
  784, 1032, 2, 2304, 2304, 2304, 8, 2304, 128, 17, 578, 2304, 128, 96, 4, 128, 128, 1032, 128, 2304, 1088, 130, 16, 512, 1, 1032, 292, 20, 34, 1032,
  2561, 1032, 128, 1032, 1032, 0, 0, 0, 528, 0, 528, 528, 528, 0, 11, 2048, 1344, 64, 36, 136, 528, 0, 260, 2048, 33, 162, 2120, 1028, 528, 2048,
  640, 2048, 2048, 273, 1024, 2048, 2, 0, 192, 2048, 8, 1288, 36, 67, 528, 2048, 36, 2048, 2048, 36, 36, 2048, 36, 2048, 1042, 2048, 2048, 512, 1, 2048,
  384, 2048, 2048, 2048, 2048, 2048, 36, 2048, 2048, 0, 3104, 385, 8, 64, 258, 1028, 528, 64, 640, 50, 4, 64, 64, 64, 2049, 24, 640, 1028, 66, 1028,
  1, 1028, 1028, 640, 640, 2048, 640, 64, 640, 1028, 296, 518, 8, 8, 8, 2192, 1, 32, 8, 1025, 272, 2048, 8, 64, 36, 768, 1154, 352, 1, 2048,
  8, 1, 1, 1028, 1, 2048, 640, 2048, 2048, 10, 1, 2048, 80, 0, 260, 1026, 8, 64, 1153, 2336, 528, 64, 2064, 517, 160, 64, 64, 64, 2, 260,
  260, 208, 260, 512, 260, 9, 2, 1064, 260, 2048, 2, 64, 2, 2, 2, 49, 8, 8, 8, 512, 2050, 132, 8, 386, 1536, 2048, 8, 64, 36, 1040,
  257, 512, 260, 2048, 8, 512, 512, 512, 1120, 2048, 65, 2048, 2048, 512, 152, 2048, 2, 64, 8, 8, 8, 64, 64, 64, 8, 64, 64, 64, 8, 64,
  64, 64, 64, 2051, 260, 544, 8, 64, 48, 1028, 2176, 64, 640, 256, 1041, 64, 64, 64, 2, 8, 8, 8, 8, 64, 8, 8, 8, 64, 8, 8,
  8, 64, 64, 64, 8, 1152, 8, 8, 8, 512, 1, 274, 8, 20, 34, 2048, 8, 64, 3328, 161, 516, 0, 192, 1026, 33, 2053, 258, 136, 528, 800,
  2064, 136, 4, 136, 1024, 136, 136, 24, 33, 33, 33, 512, 1024, 320, 33, 70, 1024, 2048, 33, 1024, 1024, 136, 1024, 192, 192, 276, 192, 512, 192, 32,
  3072, 1025, 192, 2048, 514, 18, 36, 136, 257, 512, 192, 2048, 33, 512, 512, 512, 14, 2048, 264, 2048, 2048, 512, 1024, 2048, 80, 24, 258, 2624, 4, 258,
  258, 32, 258, 1025, 4, 4, 4, 64, 258, 136, 4, 24, 24, 24, 33, 24, 258, 1028, 2176, 24, 640, 256, 4, 2080, 1024, 515, 80, 1025, 192, 32,
  8, 32, 258, 32, 32, 1025, 1025, 1025, 4, 1025, 2568, 32, 80, 24, 2052, 130, 1792, 512, 1, 32, 80, 1025, 34, 2048, 80, 388, 80, 80, 80, 1026,
  2064, 1026, 1026, 512, 40, 1026, 68, 2064, 2064, 1026, 2064, 64, 2064, 136, 257, 512, 260, 1026, 33, 512, 512, 512, 2176, 129, 2064, 256, 584, 512, 1024, 52,
  2, 512, 192, 1026, 8, 512, 512, 512, 257, 12, 2064, 96, 257, 512, 257, 257, 257, 512, 512, 512, 16, 512, 512, 512, 512, 512, 34, 2048, 1156, 512,
  512, 512, 257, 164, 513, 1026, 8, 64, 258, 17, 2176, 64, 2064, 256, 4, 64, 64, 64, 1568, 24, 1088, 256, 2176, 512, 2176, 2176, 2176, 256, 34, 256,
  256, 64, 13, 256, 2176, 2304, 8, 8, 8, 512, 1044, 32, 8, 1025, 34, 656, 8, 64, 128, 2054, 257, 512, 34, 69, 8, 512, 512, 512, 2176, 34,
  34, 256, 34, 512, 34, 1032, 80
};

void
mbe_checkGolayBlock (long int *block)
{

  static int i, syndrome, eccexpected, eccbits, databits;
  long int mask, block_l;

  block_l = *block;

  mask = 0x400000l;
  eccexpected = 0;
  for (i = 0; i < 12; i++)
    {
      if ((block_l & mask) != 0l)
        {
          eccexpected ^= golayGenerator[i];
        }
      mask = mask >> 1;
    }
  eccbits = (int) (block_l & 0x7ffl);
  syndrome = eccexpected ^ eccbits;

  databits = (int) (block_l >> 11);
  databits = databits ^ golayMatrix[syndrome];

  *block = (long) databits;
}

int
mbe_golay2312 (char *in, char *out)
{

  int i, errs;
  long block;

  block = 0;
  for (i = 22; i >= 0; i--)
    {
      block = block << 1;
      block = block + in[i];
    }

  mbe_checkGolayBlock (&block);

  for (i = 22; i >= 11; i--)
    {
      out[i] = (block & 2048) >> 11;
      block = block << 1;
    }
  for (i = 10; i >= 0; i--)
    {
      out[i] = in[i];
    }

  errs = 0;
  for (i = 22; i >= 11; i--)
    {
      if (out[i] != in[i])
        {
          errs++;
        }
    }
  return (errs);
}



int
mbe_eccAmbe3600x2450C0 (bool ambe_fr[4][24])
{

  int j, errs;
  char in[23], out[23];

  for (j = 0; j < 23; j++)
    {
      in[j] = ambe_fr[0][j + 1];
    }
  errs = mbe_golay2312 (in, out);
  // ambe_fr[0][0] should be the C0 golay24 parity bit.
  // TODO: actually test that here...
  for (j = 0; j < 23; j++)
    {
      ambe_fr[0][j + 1] = out[j];
    }

  return (errs);
}

int
mbe_eccAmbe3600x2450C1 (bool ambe_fr[4][24])
{

  int j, errs;
  char in[23], out[23];

  for (j = 0; j < 23; j++)
    {
      in[j] = ambe_fr[1][j];
    }
  errs = mbe_golay2312 (in, out);
  for (j = 0; j < 23; j++)
    {
      ambe_fr[1][j] = out[j];
    }

  return (errs);
}






/*
 * DMR AMBE interleave schedule
 */
// bit 1
const int rW[36] = {
  0, 1, 0, 1, 0, 1,
  0, 1, 0, 1, 0, 1,
  0, 1, 0, 1, 0, 1,
  0, 1, 0, 1, 0, 2,
  0, 2, 0, 2, 0, 2,
  0, 2, 0, 2, 0, 2
};

const int rX[36] = {
  23, 10, 22, 9, 21, 8,
  20, 7, 19, 6, 18, 5,
  17, 4, 16, 3, 15, 2,
  14, 1, 13, 0, 12, 10,
  11, 9, 10, 8, 9, 7,
  8, 6, 7, 5, 6, 4
};

// bit 0
const int rY[36] = {
  0, 2, 0, 2, 0, 2,
  0, 2, 0, 3, 0, 3,
  1, 3, 1, 3, 1, 3,
  1, 3, 1, 3, 1, 3,
  1, 3, 1, 3, 1, 3,
  1, 3, 1, 3, 1, 3
};

const int rZ[36] = {
  5, 3, 4, 2, 3, 1,
  2, 0, 1, 13, 0, 12,
  22, 11, 21, 10, 20, 9,
  19, 8, 18, 7, 17, 6,
  16, 5, 15, 4, 14, 3,
  13, 2, 12, 1, 11, 0
};


// This function calculates [23,12] Golay codewords.
// The format of the returned longint is [checkbits(11),data(12)].
unsigned int golay2312(unsigned int cw) {
    const unsigned int POLY = 0xAE3;            /* or use the other polynomial, 0xC75 */
    cw = cw & 0xfff;                            /* Strip off check bits and only use data */
    unsigned int c = cw;                        /* save original codeword */
    for (int i = 1; i < 13; i++) {              /* examine each data bit */
        if (cw & 1)                             /* test data bit */
            cw = cw ^ POLY;                     /* XOR polynomial */
        cw = cw >> 1;                           /* shift intermediate result */
    }
    return ((cw << 12) | c);                    /* assemble codeword */
}

// This function checks the overall parity of codeword cw.
// If parity is even, 0 is returned, else 1.
unsigned int parity(unsigned int cw) {
    /* XOR the bytes of the codeword */
    unsigned int p = cw & 0xff;
    p = p ^ ((cw >> 8) & 0xff);
    p = p ^ ((cw >> 16) & 0xff);
    
    /* XOR the halves of the intermediate result */
    p = p ^ (p >> 4);
    p = p ^ (p >> 2);
    p = p ^ (p >> 1);
    
    /* return the parity result */
    return (p & 1);
}

// Demodulate ambe frame (C1)
// Frame is an array [4][24]
void demodulateAmbe3600x2450(bool ambe_fr[4][24]) {
    unsigned short pr[115];
    unsigned short foo = 0;

    // create pseudo-random modulator
    for (int i = 23; i >= 12; i--) {
        foo = foo << 1;
        foo = foo | ambe_fr[0][i];
    }
    pr[0] = (16 * foo);
    for (int i = 1; i < 24; i++)
        pr[i] = (173 * pr[i - 1]) + 13849 - (65536 * (((173 * pr[i - 1]) + 13849) / 65536));
    for (int i = 1; i < 24; i++)
        pr[i] = pr[i] / 32768;

    // demodulate ambe_fr with pr
    int k = 1;
    for (int j = 22; j >= 0; j--) {
        ambe_fr[1][j] = ((ambe_fr[1][j]) ^ pr[k]);
        k++;
    }
}

//pick out the 49 bits of raw ambe
void get49BitRawAmbe(bool ambe_fr[4][24], unsigned char *data) {
    bool databits[7*8];
    unsigned int bitIndex = 0;

    // copy C0
    for (int j = 23; j >= 12; j--)
        databits[bitIndex++] = ambe_fr[0][j];
    
    // copy C1
    for (int j = 22; j >= 11; j--)
        databits[bitIndex++] = ambe_fr[1][j];

    // copy C2
    for (int j = 10; j >= 0; j--)
        databits[bitIndex++] = ambe_fr[2][j];

    // copy C3
    for (int j = 13; j >= 0; j--)
        databits[bitIndex++] = ambe_fr[3][j];

    // zero remaining bits
    while (bitIndex < 7*8)
        databits[bitIndex++] = 0;

    bits_to_bytes(databits, data, 7);
}

// Convert a 49 bit raw AMBE frame into a deinterleaved structure
void convert49BitAmbeTo72BitFrames(unsigned char *data, bool ambe_fr[4][24]) {
    bool ambe_d[7*8];
    bytes_to_bits(data, ambe_d, 7);
    memset(ambe_fr, 0, sizeof(bool)*4*24);

    //Place bits into the 4x24 frames.  [bit0...bit23]
    //fr0: [P e10 e9 e8 e7 e6 e5 e4 e3 e2 e1 e0 11 10 9 8 7 6 5 4 3 2 1 0]
    //fr1: [e10 e9 e8 e7 e6 e5 e4 e3 e2 e1 e0 23 22 21 20 19 18 17 16 15 14 13 12 xx]
    //fr2: [34 33 32 31 30 29 28 27 26 25 24 x x x x x x x x x x x x x]
    //fr3: [48 47 46 45 44 43 42 41 40 39 38 37 36 35 x x x x x x x x x x]

    // ecc and copy C0: 12bits + 11ecc + 1 parity
    // First get the 12 bits that actually exist
    // Then calculate the golay codeword
    // And then add the parity bit to get the final 24 bit pattern

    unsigned int tmp = 0;
    for (int i = 11; i >= 0; i--)       //grab the 12 MSB
        tmp = (tmp << 1) | ambe_d[i];
    tmp = golay2312(tmp);               //Generate the 23 bit result
    unsigned int parityBit = parity(tmp);
    tmp = tmp | (parityBit << 23);               //And create a full 24 bit value
    for (int i = 23; i >= 0; i--) {
        ambe_fr[0][i] = (tmp & 1);
        tmp = tmp >> 1;
    }

    // C1: 12 bits + 11ecc (no parity)
    tmp = 0;
    for (int i = 23; i >= 12; i--)          //grab the next 12 bits
        tmp = (tmp << 1) | ambe_d[i];
    tmp = golay2312(tmp);                    //Generate the 23 bit result
    for (int j = 22; j >= 0; j--) {
        ambe_fr[1][j] = (tmp & 1);
        tmp = tmp >> 1;
    }

    //C2: 11 bits (no ecc)
    for (int j = 10; j >= 0; j--)
        ambe_fr[2][j] = ambe_d[34 - j];

    //C3: 14 bits (no ecc)
    for (int j = 13; j >= 0; j--)
        ambe_fr[3][j] = ambe_d[48 - j];
}

void interleave(bool ambe_fr[4][24], unsigned char *data) {
    unsigned int bitIndex = 0;
    for (int i = 0; i < 36; i++) {
        data[bitIndex / 8] = ((data[bitIndex / 8] << 1) & 0xfe) | ambe_fr[rW[i]][rX[i]]; //bit 1
        bitIndex++;
        data[bitIndex / 8] = ((data[bitIndex / 8] << 1) & 0xfe) | ambe_fr[rY[i]][rZ[i]]; //bit 0
        bitIndex++;
    }
}

void deinterleave(unsigned char *data, bool ambe_fr[4][24]) {
    bool databits[72];
    bytes_to_bits(data, databits, 9);
    memset(ambe_fr, 0, sizeof(bool)*4*24);

    unsigned int bitIndex = 0;
    for (int i = 0; i < 36; i++) {
        ambe_fr[rW[i]][rX[i]] = databits[bitIndex++]; //bit 1
        ambe_fr[rY[i]][rZ[i]] = databits[bitIndex++]; //bit 0
    }
}



void ambe72toambe49(unsigned char *ambe72, unsigned char *ambe49) {
  bool ambe_fr[4][24];
  deinterleave(ambe72, ambe_fr);
  char c0err = mbe_eccAmbe3600x2450C0(ambe_fr); //Golay check/correct C0
  demodulateAmbe3600x2450(ambe_fr); //demodulate C1
  char c1err = mbe_eccAmbe3600x2450C1(ambe_fr); //Golay check/correct C1
  
  if ( (verbosity >= 1) && ((c0err > 0) || (c1err > 0)) ) {
    printf("Corrupted AMBE frame:\n"); dump(ambe72, 9);
    printf("Bit errors C0:%d, C1:%d\n", c0err, c1err);
    unsigned char tmp[9];
    bool tmp_fr[4][24];
    memcpy(tmp_fr, ambe_fr, sizeof(tmp_fr));
    demodulateAmbe3600x2450(tmp_fr); //modulate C1
    interleave(tmp_fr, tmp);
    printf("Corrected AMBE frame:\n"); dump(tmp, 9);
  }

  get49BitRawAmbe(ambe_fr, ambe49);
}

void ambe49toambe72(unsigned char *ambe49, unsigned char *ambe72) {
  bool ambe_fr[4][24];
  convert49BitAmbeTo72BitFrames(ambe49, ambe_fr);
  demodulateAmbe3600x2450(ambe_fr); //modulate C1
  interleave(ambe_fr, ambe72);
}






const unsigned char Interleave49Matrix[] = {
  0, 3, 6,  9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 41, 43, 45, 47,
  1, 4, 7, 10, 13, 16, 19, 22, 25, 28, 31, 34, 37, 40, 42, 44, 46, 48,
  2, 5, 8, 11, 14, 17, 20, 23, 26, 29, 32, 35, 38
};

void interleave49(unsigned char *data) {
  bool data_in[7*8], data_out[7*8];
  bytes_to_bits(data, data_in, 7);
  memset(data_out, 0, sizeof(data_out));
  for (int i = 0; i < 49; i++)
    data_out[Interleave49Matrix[i]] = data_in[i];
  bits_to_bytes(data_out, data, 7);
}

void deinterleave49(unsigned char *data) {
  bool data_in[7*8], data_out[7*8];
  bytes_to_bits(data, data_in, 7);
  memset(data_out, 0, sizeof(data_out));
  for (int i = 0; i < 49; i++)
    data_out[i] = data_in[Interleave49Matrix[i]];
  bits_to_bytes(data_out, data, 7);
}






void ambeServer(int portNumber) {
  struct sockaddr_in sa_read;
  struct hostent *hp;     /* host information */

  static const unsigned char AMBE_SILENCE[] = {0xAC, 0xAA, 0x40, 0x20, 0x00, 0x44, 0x40, 0x80, 0x80};

  signed char gain_in = 0;
  signed char gain_out = 0;
  unsigned short speech_rate = 2450;
  unsigned short fec_rate = 1150;

  printf("Starting md380-emu AMBEServer...\n");

  // open the socket
  int sockFd = socket(AF_INET, SOCK_DGRAM, 0);
  memset(&sa_read, 0x00, sizeof(struct sockaddr_in));
  sa_read.sin_family = AF_INET;
  sa_read.sin_port = htons(portNumber);
  sa_read.sin_addr.s_addr = htonl(INADDR_ANY);
  int ret = bind(sockFd, (struct sockaddr *)&sa_read, sizeof(struct sockaddr_in));

  printf("Listening for connections on UDP port %d\n", portNumber);

  //int pcmfd=open("migvoz.pcm", 0); //inject test audio

  while (1) {
    unsigned char buffer[1024];
    memset(buffer, 0, sizeof(buffer));

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sockFd, &fds);

    if (select(sockFd + 1, &fds, NULL, NULL, NULL) > 0) {
      socklen_t sl = sizeof(struct sockaddr_in);
      ssize_t n = recvfrom(sockFd, buffer, 1024, 0, (struct sockaddr *)&sa_read, &sl);
      if (verbosity >= 2) {
        printf("\nReceived %d bytes\n", n);
        dump(buffer, n);
      }

      if ((n > 4) && (buffer[0] == 0x61)) { //START_BYTE

        //if ( (n - 4) != ((buffer[1] << 8) | buffer[2]) ) //LENGTH
          //continue; //ignore packet if wrong length

        unsigned char *pbuffer = &buffer[4];

        if (buffer[3] == 0x00) { //CONTROL PACKET
          unsigned char outbuf[1024] = {0x61, 0x00, 0x00, 0x00};
          unsigned char *poutbuf = &outbuf[4];
          while (pbuffer <= &buffer[n-1]) { //process multiple fields
            if ((pbuffer[0] == 0x40) || (pbuffer[0] == 0x41) || (pbuffer[0] == 0x42)) { //CHANNEL FIELD
              poutbuf[0] = pbuffer[0];
              poutbuf[1] = 0x00;
              pbuffer += 1; poutbuf += 2;
            }
            else if (pbuffer[0] == 0x36) { //GETCFG
              if (verbosity >= 1) printf("CONTROL GETCFG\n");
              static const unsigned char tmp[] = {0x36, 0x05, 0x00, 0xEC};
              memcpy(poutbuf, tmp, sizeof(tmp));
              pbuffer += 1; poutbuf += sizeof(tmp);
            }
            else if (pbuffer[0] == 0x30) { //PRODID
              if (verbosity >= 1) printf("CONTROL PRODID\n");
              static const unsigned char tmp[] = {0x30, 0x41, 0x4D, 0x42, 0x45, 0x33, 0x30, 0x30, 0x30, 0x52, 0x00};
              memcpy(poutbuf, tmp, sizeof(tmp));
              pbuffer += 1; poutbuf += sizeof(tmp);
            }
            else if (pbuffer[0] == 0x31) { //VERSTRING
              if (verbosity >= 1) printf("CONTROL VERSTRING\n");
              static const unsigned char tmp[] = {0x31, 0x56, 0x31, 0x32, 0x30, 0x2E, 0x45, 0x31, 0x30, 0x30, 0x2E, 0x58, 0x58, 0x58, 0x58, 0x2E, 0x43, 0x31, 0x30, 0x36, 0x2E, 0x47, 0x35, 0x31, 0x34, 0x2E, 0x52, 0x30, 0x30, 0x39, 0x2E, 0x42, 0x30, 0x30, 0x31, 0x30, 0x34, 0x31, 0x31, 0x2E, 0x43, 0x30, 0x30, 0x32, 0x30, 0x32, 0x30, 0x38, 0x00};
              memcpy(poutbuf, tmp, sizeof(tmp));
              pbuffer += 1; poutbuf += sizeof(tmp);
            }
            else if (pbuffer[0] == 0x34) { //RESETSOFTCFG
              if (verbosity >= 1) printf("CONTROL RESETSOFTCFG\n");
              static const unsigned char tmp[] = {0x39}; //READY
              memcpy(poutbuf, tmp, sizeof(tmp));
              pbuffer += 7; poutbuf += sizeof(tmp);
            }
            else if (pbuffer[0] == 0x05) { //ECMODE
              if (verbosity >= 1) printf("CONTROL ECMODE\n");
              static const unsigned char tmp[] = {0x05, 0x00};
              memcpy(poutbuf, tmp, sizeof(tmp));
              pbuffer += 3; poutbuf += sizeof(tmp);
            }
            else if (pbuffer[0] == 0x0A) { //RATEP
              static const unsigned char RATEP_2450_1150[] = {0x04,0x31, 0x07,0x54, 0x24,0x00, 0x00,0x00, 0x00,0x00, 0x6F,0x48};
              static const unsigned char RATEP_2450_0[]    = {0x04,0x31, 0x07,0x54, 0x00,0x00, 0x00,0x00, 0x00,0x00, 0x70,0x31};
              static const unsigned char RATEP_2400_1200[] = {0x01,0x30, 0x07,0x63, 0x40,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x48};
              if (memcmp(&pbuffer[1], RATEP_2450_1150, 12) == 0) {
                speech_rate = 2450; fec_rate = 1150;
              }
              else if (memcmp(&pbuffer[1], RATEP_2450_0, 12) == 0) {
                speech_rate = 2450; fec_rate = 0;
              }
              else if (memcmp(&pbuffer[1], RATEP_2400_1200, 12) == 0) {
                speech_rate = 2400; fec_rate = 1200;
              }
              else {
                speech_rate = 0; fec_rate = 0;
              }
              if (verbosity >= 1) printf("CONTROL RATEP, SPEECH_RATE:%d FEC_RATE:%d\n", speech_rate, fec_rate);
              static const unsigned char tmp[] = {0x0A, 0x00};
              memcpy(poutbuf, tmp, sizeof(tmp));
              pbuffer += 13; poutbuf += sizeof(tmp);
            }
            else if (pbuffer[0] == 0x4B) { //GAIN
              gain_in = (signed char)pbuffer[1];
              gain_out = (signed char)pbuffer[2];
              if (verbosity >= 1) printf("CONTROL GAIN, IN:%d OUT:%d\n", gain_in, gain_out);
              static const unsigned char tmp[] = {0x4B, 0x00};
              memcpy(poutbuf, tmp, sizeof(tmp));
              pbuffer += 3; poutbuf += sizeof(tmp);
            }
            else {
              if (verbosity >= 1) printf("UNKNOWN CONTROL FIELD IDENTIFIER: 0x%02X\n", pbuffer[0]);
              break; //stop processing more fields as we don't know this field length
            }
          }
          outbuf[1] = (poutbuf-&outbuf[4]) >> 8; //packet length
          outbuf[2] = (poutbuf-&outbuf[4]) & 0xFF; //packet length
          sendto(sockFd, outbuf, poutbuf-outbuf, MSG_DONTWAIT, (struct sockaddr *)&sa_read, (ssize_t)sizeof(struct sockaddr_in));
          if (verbosity >= 2) {
            printf("CONTROL Reply\n", n);
            dump(outbuf, poutbuf-outbuf);
          }
        } //CONTROL PACKET


        else if (buffer[3] == 0x01) { //CHANNEL PACKET
          if ((pbuffer[0] == 0x40) || (pbuffer[0] == 0x41) || (pbuffer[0] == 0x42))
            pbuffer++; //ignore channel field

          if (pbuffer[0] != 0x01) //CHAND Identifier
            continue; //ignore packet

          unsigned char outbuf[6+320] = {0x61, 0x01, 0x42, 0x02,  0x00, 160};
          short *pcm = (short *)&outbuf[6];
          memset((unsigned char *)pcm, 0, 320);

          if ((pbuffer[1] == 72) && (speech_rate == 2450) && (fec_rate == 1150)) { //72-bit AMBE with FEC
            unsigned char *ambe72 = &pbuffer[2];
            unsigned char ambe49[7];
            if (verbosity >= 2) { printf("72-bit AMBE\n"); dump(ambe72, 9); }
            ambe72toambe49(ambe72, ambe49);
            if (verbosity >= 2) { printf("49-bit AMBE\n"); dump(ambe49, 7); }
            decode_amb_buffer(ambe49, pcm);
          }
          else if ((pbuffer[1] == 49) && (speech_rate == 2450) && (fec_rate == 0)) { //49-bit AMBE without FEC
            unsigned char *ambe49 = &pbuffer[2];
            deinterleave49(ambe49);
            if (verbosity >= 2) { printf("49-bit AMBE\n"); dump(ambe49, 7); }
            decode_amb_buffer(ambe49, pcm);
          }
          else continue; //unsupported rate

          //if (read(pcmfd, pcm, 320) != 320) lseek(pcmfd, 0, SEEK_SET); //inject test audio
          if (gain_out != 0) {
            double gain_m = pow(10, gain_out/20.0);
            for (int i=0; i < 160; i++) {
              int tmp = pcm[i] * gain_m;
              if (tmp > 32767) tmp = 32767;
              else if (tmp < -32768) tmp = -32768;
              pcm[i] = tmp;
            }
          }
          for (int i=0; i < 160; i++) //swap byte order for all samples, AMBE3000 uses MSB first
            ((unsigned short *)pcm)[i] = (((unsigned short *)pcm)[i] >> 8) | (((unsigned short *)pcm)[i] << 8);
          sendto(sockFd, outbuf, sizeof(outbuf), MSG_DONTWAIT, (struct sockaddr *)&sa_read, (ssize_t)sizeof(struct sockaddr_in));
          if (verbosity >= 2) {
            printf("PCM Reply\n");
            dump(outbuf, sizeof(outbuf));
          }
        } //CHANNEL PACKET


        else if (buffer[3] == 0x02) { //SPEECH PACKET
          if ((pbuffer[0] == 0x40) || (pbuffer[0] == 0x41) || (pbuffer[0] == 0x42))
            pbuffer++; //ignore channel field

          if ((pbuffer[0] != 0x00) || (pbuffer[1] != 160)) //SPEECHD identifier, Samples
            continue; //ignore packet
            
          short *pcm = (short *)&pbuffer[2];
          
          for (int i=0; i < 160; i++) //swap byte order for all samples, AMBE3000 uses MSB first
            ((unsigned short *)pcm)[i] = (((unsigned short *)pcm)[i] >> 8) | (((unsigned short *)pcm)[i] << 8);
          //if (read(pcmfd, pcm, 320) != 320) lseek(pcmfd, 0, SEEK_SET); //inject test audio
          if (gain_in != 0) {
            double gain_m = pow(10, gain_in/20.0);
            for (int i=0; i < 160; i++) {
              int tmp = pcm[i] * gain_m;
              if (tmp > 32767) tmp = 32767;
              else if (tmp < -32768) tmp = -32768;
              pcm[i] = tmp;
            }
          }

          if ((speech_rate == 2450) && (fec_rate == 1150)) { //72-bit AMBE with FEC
            unsigned char outbuf[6+9] = {0x61, 0x00, 0x0B, 0x01,  0x01, 72};
            unsigned char *ambe72 = &outbuf[6];
            unsigned char ambe49[7];
            memset(ambe49, 0, 7);
            encode_amb_buffer(ambe49, pcm);
            ambe49toambe72(ambe49, ambe72);
            //memcpy(ambe72, AMBE_SILENCE, 9); //dbg
            sendto(sockFd, outbuf, sizeof(outbuf), MSG_DONTWAIT, (struct sockaddr *)&sa_read, (ssize_t)sizeof(struct sockaddr_in));
            if (verbosity >= 2) {
              printf("72-bit AMBE Reply\n");
              dump(outbuf, sizeof(outbuf));
            }
          }
          else if ((speech_rate == 2450) && (fec_rate == 0)) { //49-bit AMBE without FEC
            unsigned char outbuf[6+7] = {0x61, 0x00, 0x09, 0x01,  0x01, 49};
            unsigned char *ambe49 = &outbuf[6];
            memset(ambe49, 0, 7);
            encode_amb_buffer(ambe49, pcm);
            interleave49(ambe49);
            sendto(sockFd, outbuf, sizeof(outbuf), MSG_DONTWAIT, (struct sockaddr *)&sa_read, (ssize_t)sizeof(struct sockaddr_in));
            if (verbosity >= 2) {
              printf("49-bit AMBE Reply\n");
              dump(outbuf, sizeof(outbuf));
            }
          }
        } //SPEECH PACKET

      } //START_BYTE

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
