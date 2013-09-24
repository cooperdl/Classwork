/* Copyright (c) 2011, RidgeRun
 * All rights reserved.
 *
From https://www.ridgerun.com/developer/wiki/index.php/Gpio-int-test.c

 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the RidgeRun.
 * 4. Neither the name of the RidgeRun nor the
 *    names of its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY RIDGERUN ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL RIDGERUN BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>	// Defines signal-handling functions (i.e. trap Ctrl-C)
#include "gpio-utils.h"

#define _BSD_SOURCE 1

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>	/* for NAME_MAX */
#include <strings.h>	/* for strcasecmp() */
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>
#include "i2cbusses.h"
#include "i2c-dev.h"

 /****************************************************************
 * Constants
 ****************************************************************/
 
#define POLL_TIMEOUT (3 * 1000) /* 3 seconds */
#define GPIOIN1 30
#define GPIOIN2 60
#define GPIOIN3 31
#define GPIOIN4 48
#define GPIOIN5 3
#define GPIOIN6 2
#define WIDTH 8
#define HEIGHT 8
#define MAX_BUF 64

/****************************************************************
 * Global variables
 ****************************************************************/
int keepgoing = 1;	// Set to 0 when ctrl-c is pressed

/****************************************************************
 * signal_handler
 ****************************************************************/
void signal_handler(int sig);
// Callback called when SIGINT is sent to the process (Ctrl-C)
void signal_handler(int sig)
{
	printf( "Ctrl-C pressed, cleaning up and exiting..\n" );
	keepgoing = 0;
}

/****************************************************************
 * Main
 ****************************************************************/


#define BICOLOR		// undef if using a single color display

// The upper btye is RED, the lower is GREEN.
// The single color display responds only to the lower byte
static __u16 smile_bmp[]=
	{0x3c, 0x42, 0xa9, 0x85, 0x85, 0xa9, 0x42, 0x3c};
static __u16 frown_bmp[]=
	{0x3c00, 0x4200, 0xa900, 0x8500, 0x8500, 0xa900, 0x4200, 0x3c00};
static __u16 neutral_bmp[]=
	{0x3c3c, 0x4242, 0xa9a9, 0x8989, 0x8989, 0xa9a9, 0x4242, 0x3c3c};

static void help(void) __attribute__ ((noreturn));

static void help(void) {
	fprintf(stderr, "Usage: matrixLEDi2c (hardwired to bus 3, address 0x70)\n");
	exit(1);
}

static int check_funcs(int file) {
	unsigned long funcs;

	/* check adapter functionality */
	if (ioctl(file, I2C_FUNCS, &funcs) < 0) {
		fprintf(stderr, "Error: Could not get the adapter "
			"functionality matrix: %s\n", strerror(errno));
		return -1;
	}

	if (!(funcs & I2C_FUNC_SMBUS_WRITE_BYTE)) {
		fprintf(stderr, MISSING_FUNC_FMT, "SMBus send byte");
		return -1;
	}
	return 0;
}

// Writes block of data to the display
static int write_block(int file, __u16 *data) {
	int res;
#ifdef BICOLOR
	res = i2c_smbus_write_i2c_block_data(file, 0x00, 16, 
		(__u8 *)data);
	return res;
#else
/*
 * For some reason the single color display is rotated one column, 
 * so pre-unrotate the data.
 */
	int i;
	__u16 block[I2C_SMBUS_BLOCK_MAX];
//	printf("rotating\n");
	for(i=0; i<8; i++) {
		block[i] = (data[i]&0xfe) >> 1 | 
			   (data[i]&0x01) << 7;
	}
	res = i2c_smbus_write_i2c_block_data(file, 0x00, 16, 
		(__u8 *)block);
	return res;
#endif
}



void printboard(char board[HEIGHT][WIDTH],int file,int color);
int main(int argc, char **argv, char **envp)
{
	struct pollfd fdset[7];
	int nfds = 7;
	int gpio_fd1, gpio_fd2, gpio_fd3, gpio_fd4,gpio_fd5,gpio_fd6, timeout, rc;
	char buf[MAX_BUF];
	unsigned int gpio;
	int len;
	char board[HEIGHT][WIDTH];
	int xpos=WIDTH/2;
	int ypos=HEIGHT/2;
        int row;
	int col;
	int colorint=0;
	char color[]={'x','y','z'};	
	for(row=0;row<HEIGHT;row++){
		printf("\n");
		for(col=0;col<WIDTH;col++){
			board[row][col]=' ';
		}
	}

	int res, i2cbus, address, file;
	char filename[20];
	int force = 0;

	i2cbus = lookup_i2c_bus("1");
	printf("i2cbus = %d\n", i2cbus);
	if (i2cbus < 0)
		help();

	address = parse_i2c_address("0x70");
	printf("address = 0x%2x\n", address);
	if (address < 0)
		help();

	file = open_i2c_dev(i2cbus, filename, sizeof(filename), 0);
//	printf("file = %d\n", file);
	if (file < 0
	 || check_funcs(file)
	 || set_slave_addr(file, address, force))
		exit(1);

	// Check the return value on these if there is trouble
	i2c_smbus_write_byte(file, 0x21); // Start oscillator (p10)
	i2c_smbus_write_byte(file, 0x81); // Disp on, blink off (p11)
	i2c_smbus_write_byte(file, 0xe7); // Full brightness (page 15)

//	Display a series of pictures
	

	system("/home/root/homework3/i2csetup.sh");
	board[xpos][ypos]='x';
	printboard(board,file,colorint);
	//printf("wgat");
	/*if (argc < 2) {
		printf("Usage: gpio-int <gpio-pin>\n\n");
		printf("Waits for a change in the GPIO pin voltage level or input on stdin\n");


		FILE* f = fopen("/sys/class/leds/beaglebone\:green\:usr0/brightness", "w");
                FILE* trig = fopen("/sys/class/leds/beaglebone\:green\:usr0/trigger", "w");
 
    		if (trig == NULL){
			printf("oops\n");        			
			exit(EXIT_FAILURE);
		}
    		fprintf(trig, "none");
    		fclose(trig);
		if (fled== NULL){
			printf("oops\n");        			
			exit(EXIT_FAILURE);
		}
		printf("should light up\n");
    		fprintf(fled, "1");
    		fclose(fled);
		usleep(10000000);
		exit(-1);
	}
		*/
	// Set the signal callback for Ctrl-C
	signal(SIGINT, signal_handler);


	gpio_export(GPIOIN1);
	gpio_set_dir(GPIOIN1, "in");
	gpio_set_edge(GPIOIN1, "both");  // Can be rising, falling or both
	gpio_fd1 = gpio_fd_open(GPIOIN1, O_RDONLY);

	gpio_export(GPIOIN2);
	gpio_set_dir(GPIOIN2, "in");
	gpio_set_edge(GPIOIN2, "both");  // Can be rising, falling or both
	gpio_fd2 = gpio_fd_open(GPIOIN2, O_RDONLY);
	gpio_export(GPIOIN3);
	gpio_set_dir(GPIOIN3, "in");
	gpio_set_edge(GPIOIN3, "both");  // Can be rising, falling or both
	gpio_fd3 = gpio_fd_open(GPIOIN3, O_RDONLY);

	gpio_export(GPIOIN4);
	gpio_set_dir(GPIOIN4, "in");
	gpio_set_edge(GPIOIN4, "both");  // Can be rising, falling or both
	gpio_fd4 = gpio_fd_open(GPIOIN4, O_RDONLY);

	gpio_export(GPIOIN5);
	gpio_set_dir(GPIOIN5, "in");
	gpio_set_edge(GPIOIN5, "both");  // Can be rising, falling or both
	gpio_fd5 = gpio_fd_open(GPIOIN5, O_RDONLY);

	gpio_export(GPIOIN6);
	gpio_set_dir(GPIOIN6, "in");
	gpio_set_edge(GPIOIN6, "both");  // Can be rising, falling or both
	gpio_fd6 = gpio_fd_open(GPIOIN6, O_RDONLY);

	timeout = POLL_TIMEOUT;
 	FILE* f0 = fopen("/sys/class/leds/beaglebone:green:usr0/trigger", "w");
	FILE* f1 = fopen("/sys/class/leds/beaglebone:green:usr1/trigger", "w");
	FILE* f2 = fopen("/sys/class/leds/beaglebone:green:usr2/trigger", "w");
	FILE* f3 = fopen("/sys/class/leds/beaglebone:green:usr3/trigger", "w");

    	if (f0 == NULL){
		exit(EXIT_FAILURE);
	}
	fprintf(f0, "none");
	fclose(f0);

	if (f1 == NULL){
		exit(EXIT_FAILURE);
	}
	fprintf(f1, "none");
	fclose(f1);

	if (f2 == NULL){
		exit(EXIT_FAILURE);
	}
	fprintf(f2, "none");
	fclose(f2);
	
	if (f3 == NULL){
		exit(EXIT_FAILURE);
	}
	fprintf(f3, "none");
	fclose(f3);
	
			
	while (keepgoing) {
		memset((void*)fdset, 0, sizeof(fdset));
		
		fdset[0].fd = STDIN_FILENO;
		fdset[0].events = POLLIN;
      
		fdset[1].fd = gpio_fd1;
		fdset[1].events = POLLPRI;

		fdset[2].fd = gpio_fd2;
		fdset[2].events = POLLPRI;

		fdset[3].fd = gpio_fd3;
		fdset[3].events = POLLPRI;

		fdset[4].fd = gpio_fd4;
		fdset[4].events = POLLPRI;

		fdset[5].fd = gpio_fd5;
		fdset[5].events = POLLPRI;

		fdset[6].fd = gpio_fd6;
		fdset[6].events = POLLPRI;


		rc = poll(fdset, nfds, timeout);      

		if (rc < 0) {
			printf("\npoll() failed!\n");
			return -1;
		}
      
		
            
		if (fdset[1].revents & POLLPRI) {
			lseek(fdset[1].fd, 0, SEEK_SET);  // Read from the start of the file
			read(fdset[1].fd, buf, MAX_BUF);
			FILE* f = fopen("/sys/class/leds/beaglebone:green:usr3/brightness", "w");
 
    			if (f == NULL){        			
				exit(EXIT_FAILURE);
			}
			if (buf[0]=='1'){
				fprintf(f, "0");
			}
			if (buf[0]=='0'){
				fprintf(f, "1");
				if((xpos+1)<WIDTH){
					board[xpos+1][ypos]='x';
					xpos++;
				}
				printboard(board,file,colorint);
			}
			fclose(f);
				
		}
		if (fdset[2].revents & POLLPRI) {
			lseek(fdset[2].fd, 0, SEEK_SET);  // Read from the start of the file
			read(fdset[2].fd, buf, MAX_BUF);
			FILE* f = fopen("/sys/class/leds/beaglebone:green:usr2/brightness", "w");
 
    			if (f == NULL){        			
				exit(EXIT_FAILURE);
			}
			if (buf[0]=='1'){
				fprintf(f, "0");
			}
			if (buf[0]=='0'){
				fprintf(f, "1");
				if((xpos-1)>=0){
					board[xpos-1][ypos]='x';
					xpos--;
				}
				
				printboard(board,file,colorint);

			}
			fclose(f);
				
		}
		if (fdset[3].revents & POLLPRI) {
			lseek(fdset[3].fd, 0, SEEK_SET);  // Read from the start of the file
			read(fdset[3].fd, buf, MAX_BUF);
			FILE* f = fopen("/sys/class/leds/beaglebone:green:usr1/brightness", "w");
 
    			if (f == NULL){        			
				exit(EXIT_FAILURE);
			}
			if (buf[0]=='1'){
				fprintf(f, "0");
			}
			if (buf[0]=='0'){
				if((ypos+1)<HEIGHT){
					board[xpos][ypos+1]='x';
					ypos++;
				}
				printboard(board,file,colorint);
				fprintf(f, "1");
			}
			fclose(f);
				
		}
		if (fdset[4].revents & POLLPRI) {
			lseek(fdset[4].fd, 0, SEEK_SET);  // Read from the start of the file
			read(fdset[4].fd, buf, MAX_BUF);
			FILE* f = fopen("/sys/class/leds/beaglebone:green:usr0/brightness", "w");
 
    			if (f == NULL){        			
				exit(EXIT_FAILURE);
			}
			if (buf[0]=='1'){
				fprintf(f, "0");
			}
			if (buf[0]=='0'){
				if((ypos-1)>=0){
					board[xpos][ypos-1]='x';
					ypos--;
				}
				printboard(board,file,colorint);
				fprintf(f, "1");

			}
			fclose(f);
				
		}
		if (fdset[5].revents & POLLPRI) {
			lseek(fdset[5].fd, 0, SEEK_SET);  // Read from the start of the file
			read(fdset[5].fd, buf, MAX_BUF);
			
			if (buf[0]=='0'){
				for(row=0;row<HEIGHT;row++){
				printf("\n");
					for(col=0;col<WIDTH;col++){
						board[row][col]=' ';
					}
					printf("\n");
					system("i2cget -y 1 0x48 0");
					printboard(board,file,colorint);
				}
				
				

			}
			
				
		}
		if (fdset[6].revents & POLLPRI) {
			lseek(fdset[6].fd, 0, SEEK_SET);  // Read from the start of the file
			read(fdset[6].fd, buf, MAX_BUF);
			
			if (buf[0]=='0'){
				if(colorint<=0){
					colorint++;
				}else{
					colorint=0;
				}
				printf("\n");
				system("i2cget -y 1 0x4a 0");
				printboard(board,file,colorint);

			}
			
			
			
				
		}
		/*if (fdset[0].revents & POLLIN) {
			(void)read(fdset[0].fd, buf, 1);
			printf("\npoll() stdin read 0x%2.2X\n", (unsigned int) buf[0]);
		}*/
		
		
		
		fflush(stdout);
	}
	
	
	gpio_fd_close(gpio_fd1);
	gpio_fd_close(gpio_fd2);
	gpio_fd_close(gpio_fd3);
	gpio_fd_close(gpio_fd4);
	return 0;
}
void printboard(char board[HEIGHT][WIDTH], int file,int color){
	printf("\n****new board****");
        int i;
	int j;	
	__u16 display[8]={0,0,0,0,0,0,0,0};
	for(i=0;i<HEIGHT;i++){
		printf("\n");
		for(j=0;j<WIDTH;j++){
			printf("%c",board[i][WIDTH-1-j]);
			if(board[i][j]!=' '){
				display[i]++;
			}
			if(j!=WIDTH-1){
				display[i]=display[i]<<1;
			}
		}
	}
	
	if(color==1){	
		for(j=0;j<WIDTH;j++){
			display[j]=display[j]<<8;
		}
	}
	fflush(stdout);
	
	write_block(file, display);


		




}

