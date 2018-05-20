/* 
 * File:   main.c
 * Author: tdarlic
 *
 * Created on May 14, 2018, 7:06 PM
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h> 
#include <string.h>
#include <features.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

int set_interface_attribs(int fd, int speed, int parity) {
    struct termios tty;
    memset(&tty, 0, sizeof tty);
    if (tcgetattr(fd, &tty) != 0) {
        printf("error %d from tcgetattr", errno);
        return -1;
    }

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8-bit chars
    // disable IGNBRK for mismatched speed tests; otherwise receive break
    // as \000 chars
    tty.c_iflag &= ~IGNBRK; // disable break processing
    tty.c_lflag = 0; // no signaling chars, no echo,
    // no canonical processing
    tty.c_oflag = 0; // no remapping, no delays
    tty.c_cc[VMIN] = 0; // read doesn't block
    tty.c_cc[VTIME] = 5; // 0.5 seconds read timeout

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

    tty.c_cflag |= (CLOCAL | CREAD); // ignore modem controls,
    // enable reading
    tty.c_cflag &= ~(PARENB | PARODD); // shut off parity
    tty.c_cflag |= parity;
    tty.c_cflag &= ~CSTOPB;
    //    tty.c_cflag &= ~CRTSCTS;


    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("error %d from tcsetattr", errno);
        return -1;
    }
    return 0;
}

void set_blocking(int fd, int should_block) {
    struct termios tty;
    memset(&tty, 0, sizeof tty);
    if (tcgetattr(fd, &tty) != 0) {
        printf("error %d from tggetattr", errno);
        return;
    }

    tty.c_cc[VMIN] = should_block ? 1 : 0;
    tty.c_cc[VTIME] = 5; // 0.5 seconds read timeout

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("error %d setting term attributes", errno);
    }
}

int get_response(char * expected_response, int timeout, int port) {
    unsigned long timerec = time(NULL);
    char rcvbuf[300];
    int answer = 0;
    int n = 0;
    char timebuf[30];
    char buf[300];
    memset(rcvbuf, 0, 300);
    memset(buf, 0, 300);
    do {
        n = read(port, &buf, sizeof buf); // read up to 16 characters if ready to read
        if (strlen(buf) > 0) {

            strcat(rcvbuf, buf);
            memset(buf, 0, 300);
            if (strstr(rcvbuf, expected_response) != NULL) {
                answer = 1;
                clear_uart_bufer(port);
            }
        }
    } while ((answer == 0) || ((time(NULL) - timerec) > timeout));
    return answer;
}

int get_address(int timeout, int port) {
    unsigned long timerec = time(NULL);
    char buf[300];
    char rcvbuf[500];
    memset(rcvbuf, 0, 500);
    memset(buf, 0, 300);
    int n = 0;
    do {
        n = read(port, &buf, sizeof buf);
        if (strlen(buf) > 0) {

            strcat(rcvbuf, buf);
            memset(buf, 0, 256);
            if (strstr(rcvbuf, "\n") != NULL) {
                return atoi(rcvbuf);
            }
        }
    } while (((time(NULL) - timerec) > timeout));
    return 0;
}

/**
 * Checks if file exists by trying to open it in read mode
 * @param filename Name of the file
 * @return 1 if file exists 0 if file does not exist
 */
bool file_exists(char * filename) {
    FILE *file;
    file = fopen(filename, "r");
    if (file) {
        // file exists    
        fclose(file);
        return true;
    }
    // file does not exist 
    return false;
}

void print_uart_buffer() {

}

void clear_uart_bufer(int port) {
    int n = 0;
    char buf[300];
    n = read(port, &buf, sizeof buf);
}

int write_last_address(int address) {
    FILE *f;
    f = fopen("lastaddr.txt", "w");
    if (f == NULL) {
        printf("Error opening file lastaddr.txt!\n");
        return 0;
    }
    fprintf(f, "%i\n", address);
    fclose(f);
}

int read_last_address() {
    FILE *f;
    char addrstr[30];
    memset(addrstr, 0, 30);
    size_t len = 0;
    ssize_t read;
    f = fopen("lastaddr.txt", "r");
    if (f == NULL) {
        printf("Error opening file lastaddr.txt!\n");
        return 0;
    }
    read = fgets(addrstr, 28, f);
    if (read == -1) {
        return 0;
    }
    fclose(f);
    return atoi(addrstr);
}

/*
 * 
 */
int main(int argc, char** argv) {
    char ready[] = "Flash erased, upload binary file...";
    const char *portname = "/dev/ttyUSB0";
    unsigned char buf[300];
    int rcnt = 0;
    int address = 0;
    int old_address = 0;
    FILE *bfile;
    int perc_complete = 0;
    float percfl = 0;
    int firstaddr = 0;
    int zeroaddrerr = 0;
    int send_last_addr = 0;
    char last_addr_ch[20];

    memset(last_addr_ch, 0, 20);

    if (argc < 2) {
        printf("File name missing\n");
        exit(-1);
    }

    if (!file_exists(argv[1])) {
        printf("ERROR: file does not exist: %s\n", argv[1]);
        fprintf(stderr, "error opening file '%s' mode 'r': %s\n",
                argv[1], strerror(errno));
        exit(-1);
    }

    if (argc > 2) {
        if (0 == strcmp(argv[2], "-a")) {
            send_last_addr = read_last_address();
        }
    } else {
        send_last_addr = 0;
    }

    printf("Opening file: %s\n", argv[1]);

    // open binary file for reading
    bfile = fopen(argv[1], "rb");


    printf("Connecting to %s\n", portname);

    int fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        printf("error %d opening %s: %s", errno, portname, strerror(errno));
        return (EXIT_FAILURE);
    }
    set_interface_attribs(fd, B57600, 0); // set speed to 57600 bps, 8n1 (no parity)
    set_blocking(fd, 0); // set no blocking

    printf("Waiting 300 sec for the board to be ready.\n");
    // wait for the board to send ready string
    clear_uart_bufer(fd);
    if (get_response(ready, 300, fd)) {
        write(fd, "sender_ready\n", 14); // send initial string
        printf("Board ready - starting\n");
    } else {
        printf("No response from board!");
        return (EXIT_FAILURE);
    }
    printf("First address:%i\n", send_last_addr);
    sprintf(last_addr_ch, "%i\n", send_last_addr);
    write(fd, last_addr_ch, strlen(last_addr_ch));
    clear_uart_bufer(fd);
    address = get_address(2, fd);
    do {
        memset(buf, 0, 300);
        fseek(bfile, address, SEEK_SET);
        rcnt = fread(buf, 1, 256, bfile);
        write(fd, buf, rcnt);
        percfl = (((float) address / (float) 8388608) * 100);
        perc_complete = (int) percfl;
        printf("%d%% - %x\r", perc_complete, address);
        fflush(stdout);

        address = get_address(2, fd);
        if ((address < old_address) && (address != -1)) {
            // zero address received - possible error in timing, retry
            if (address == 0) {
                if (zeroaddrerr > 5) {
                    printf("\nZero address received %i times, exiting.\n", zeroaddrerr);
                    write_last_address(old_address);
                    break;
                }
                printf("\nZero address received, trying again...\n");
                zeroaddrerr++;
                continue;
            }
            printf("\nError - address %d is smaller than old address %d \n", address, old_address);
            write_last_address(old_address);
            break;
        }
        // mark as first zero address received
        if (address == 0) {
            firstaddr = 1;
        }

        old_address = address;
    } while (address > -1);

    printf("Last address: %d\n", address);

    fclose(bfile);

    return (EXIT_SUCCESS);
}

