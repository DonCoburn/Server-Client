#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "crc16.h"
#include "xmodemserver.h"
#include "helper.h"

#ifndef PORT
  #define PORT 2734
#endif

#define MAX_QUEUE 10
#define FNAME_LEN 20
#define PAYLOAD_A 128
#define PAYLOAD_B 1024

struct client *head = NULL;
char *dir = "filestore";
char *extension = ".don";

static void manage_client(struct client *p);

void addclient(struct client *p) {
    p->next = head;
    head = p;
    printf("--New client added!--\n");
}

void dropclient(int fd) {
    
    struct client *temp = head, *prev;

    if (temp && temp->fd == fd) {
        head = temp->next;
        free(temp); 
        printf("--Dropped client!--\n");
        return;
    }
 
    while (temp && (temp->fd != fd)) {
        prev = temp;
        temp = temp->next;
    }
 
    if (temp) {
        prev->next = temp->next;
        free(temp);
        printf("--Dropped client!--\n");
        return;
    }
	fprintf(stderr, "Cannot find fd %d, aborting...\n", fd);
}

void finish_(struct client *p) {
    printf("Entering finished state.\n");
    p->state = finished;
    p->current_block = 0;
    if (p->fp >= 0) { fclose(p->fp); }
}
    
void check_block_(struct client *p) {
    extern void manage_client(struct client *p);

    printf("Entering check block state.\n");

    if (p->block_read != (255 - p->inverse)) {
        fprintf(stderr, "Current block and inverse do not match, client dropped...\n");
        finish_(p);
        dropclient(p->fd);
    } else if (p->current_block == p->block_read) {
        char temp = ACK;
        write(p->fd, &temp, 1);
        manage_client(p);
    } else if (p->block_read != p->current_block + 1) {
        fprintf(stderr, "Expected block %d but %d was given, client dropped...\n", 
        p->current_block + 1, p->block_read);
        finish_(p);
        dropclient(p->fd);
    } else {
        unsigned short crc = crc_message(XMODEM_KEY, p->buf, 128);
        unsigned char temp = crc >> 8;
        if ((temp != p->high_byte) || ((temp = crc) != p->low_byte)) {
            char temp = NAK;
            write(p->fd, &temp, 1);
            p->state = get_block;
            manage_client(p);
        } else {
            fseek(p->fp, 0, SEEK_CUR);
            fprintf(p->fp, "%s", p->buf);
            p->current_block++;
            if (p->current_block > 255) { p->current_block = 0; }
            char temp = ACK;
            write(p->fd, &temp, 1);
            p->state = pre_block;
            manage_client(p);
        }
    }
}

void get_block_(struct client *p) { 
    int len;

    printf("Entering get block state.\n");

    fd_set fds1;
    FD_ZERO(&fds1);
    FD_SET(p->fd, &fds1);

    if (select(p->fd+1, &fds1, NULL, NULL, NULL) < 0) { perror("select"); }

    if (FD_ISSET(p->fd, &fds1)) {
        len = read(p->fd, &p->block_read, 1);
        if (len <= 0) {
            finish_(p);
            fprintf(stderr, "Block number not received: server dropped client\n");
            dropclient(p->fd);
            return;
        }
    } 

    FD_SET(p->fd, &fds1);
    if (select(p->fd+1, &fds1, NULL, NULL, NULL) < 0) { perror("select"); }
    
    if (FD_ISSET(p->fd, &fds1)) {
        len = read(p->fd, &p->inverse, 1);
        if (len <= 0) {
            finish_(p);
            fprintf(stderr, "Inverse not received: server dropped client\n");
            dropclient(p->fd);
            return;
        }
    }

    FD_SET(p->fd, &fds1);
    if (select(p->fd+1, &fds1, NULL, NULL, NULL) < 0) { perror("select"); }
    
    if (FD_ISSET(p->fd, &fds1)) {
        len = read(p->fd, p->buf, p->blocksize);
        while (len < p->blocksize) {
            FD_SET(p->fd, &fds1);
            if (select(p->fd+1, &fds1, NULL, NULL, NULL) < 0) { perror("select"); }
            
            if (FD_ISSET(p->fd, &fds1)) { len += read(p->fd, &p->buf[len], p->blocksize-len); }
        }
        if (len <= 0) {
            finish_(p);
            fprintf(stderr, "Payload not received: server dropped client\n");
            dropclient(p->fd);
            return;
        }
    }

    FD_SET(p->fd, &fds1);
    if (select(p->fd+1, &fds1, NULL, NULL, NULL) < 0) { perror("select"); }
    
    if (FD_ISSET(p->fd, &fds1)) {
        len = read(p->fd, &p->high_byte, 1);
        if (len <= 0) {
            finish_(p);
            fprintf(stderr, "CRC16 high byte not received: server dropped client\n");
            dropclient(p->fd);
            return;
        }
    }

    FD_SET(p->fd, &fds1);
    if (select(p->fd+1, &fds1, NULL, NULL, NULL) < 0) { perror("select"); }
    
    if (FD_ISSET(p->fd, &fds1)) {
        len = read(p->fd, &p->low_byte, 1);
        if (len <= 0) {
            finish_(p);
            fprintf(stderr, "CRC16 low byte not received: server dropped client\n");
            dropclient(p->fd);
            return;
        }
    }
    p->state = check_block;
    check_block_(p);
}

void pre_block_(struct client *p) {
    char asxii = '\0';
    int len;

    printf("Entering pre block state.\n");

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(p->fd, &fds);

    if (select(p->fd+1, &fds, NULL, NULL, NULL) < 0) { perror("select"); }

    if (FD_ISSET(p->fd, &fds)) {
        while ((len = read(p->fd, &asxii, 1)) > 0) {
            if (asxii == EOT) {
                char response = ACK;
                write(p->fd, &response, 1);
                finish_(p);
                dropclient(p->fd);
                break;
            }
            if (asxii == SOH) {
                p->blocksize = PAYLOAD_A;
                p->state = get_block;
                get_block_(p);
                break;
            }
            if (asxii == STX) {
                p->blocksize = PAYLOAD_B;
                p->state = get_block;
                get_block_(p);
                break;
            }
        }
        if (len <= 0) {
            dropclient(p->fd);
            fprintf(stderr, "ASCII not received: server dropped client\n");
            return;
        }
    }
}

void initial_(struct client *p) {

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(p->fd, &fds);

    if (select(p->fd+1, &fds, NULL, NULL, NULL) < 0) { perror("select"); }
    
    char filename[FNAME_LEN+strlen(extension)+1];
    char line[MAXBUFFER];
    printf("Entering initial state.\nWaiting on filename...\n");

    int num_chars;

    if (FD_ISSET(p->fd, &fds)) {
        num_chars = read(p->fd, line, MAXBUFFER);
        line[num_chars] = '\0';
    }

    // reading until the max len is reached or "\r\n" is encountered.
    while (strstr(line, "\r\n") == NULL) {
        if (num_chars == MAXBUFFER) {
            fprintf(stderr, "Filename too long: server dropped client\n");
            return;
        }

        FD_SET(p->fd, &fds);
        if (select(p->fd+1, &fds, NULL, NULL, NULL) < 0) { perror("select"); }

        if (FD_ISSET(p->fd, &fds)) { 
            num_chars += read(p->fd, &line[num_chars], MAXBUFFER-num_chars);
            line[num_chars]='\0';
        }
    }
    if (num_chars <= 0) {
        fprintf(stderr, "Filename not received: server dropped client\n");
        return;
    }

    strncpy(filename, line, FNAME_LEN-1);
    
    int index;
    for (int i = 0; i < strlen(filename); i++) {
        if(filename[i] == '\r') {
            index = i;
            break;
        }
    }
    if (index) filename[index] = '\0';
    
    int end = strlen(filename) - 1;
    if ((filename[end-1] == '.') && (filename[end] == 'c')) { strcat(filename, extension); }
    filename[strlen(filename)] = '\0';
    FILE *fp;

    if ((fp = open_file_in_dir(filename, dir)) == NULL) {
        perror("open file");
        fclose(fp);
        return;
    }
    
    printf("-->%s opened!\n", filename);
    char response = 'C';
    write(p->fd, &response, 1);

    // add client finallly and move to pre_block
    strcpy(p->filename, filename);
    p->fp = fp;
    if (!p->current_block) { p->current_block = 0; }
    p->state = pre_block;
    addclient(p);
    pre_block_(p);
}

static void manage_client(struct client *p) {
    switch (p->state) {
        case initial:
            initial_(p);
            break;
        case pre_block:
            pre_block_(p);
            break;
        case get_block:
            get_block_(p);
            break;
        case check_block:
            check_block_(p);
            break;
        default:
            fprintf(stderr, "invalid state, dropping client...\n");
            finish_(p);
    }
}

void new_connection(int sock, struct sockaddr_in client_addr, unsigned int client_len) {
    struct client *p = malloc(sizeof(struct client));

    int client_fd = accept(sock, (struct sockaddr *) &client_addr, &client_len);
    if (client_fd == -1) {
        perror("accept");
        exit(1);
    }
    printf("--Connection Established!--\n");
    p->fd = client_fd;
    initial_(p);
}

int main(int argc, char **argv) { // taken from muffinman.c & code posted from lecture
    struct client *p; 

    // create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("server: socket");
        exit(1);
    }

    // initialize server address
    struct sockaddr_in server, client_addr;
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    memset(&server.sin_zero, 0, sizeof(server)); 
    server.sin_addr.s_addr = INADDR_ANY; 
    unsigned int client_len = sizeof(struct sockaddr_in);

    int yes = 1;
    if ((setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1) { 
        perror("setsockopt");
        exit(1);
    }

    // bind socket to an address
    if (bind(sock, (struct sockaddr *) &server, sizeof(struct sockaddr_in)) == -1) {
      perror("server: bind");
      close(sock);
      exit(1);
    }

    // Set up a queue in the kernel to hold pending connections.
    if (listen(sock, MAX_QUEUE) < 0) {
        perror("listen");
        close(sock);
        exit(1);
    }

    while (1) { // taken from muffinman.c
        fd_set fds;
        int maxfds = sock;
	    FD_ZERO(&fds);
	    FD_SET(sock, &fds);

        // check the already exisiting connections for changes 
        for (p = head; p; p = p->next) {
            FD_SET(p->fd, &fds);
            if (p->fd > maxfds) { maxfds = p->fd; }
        }

        if (select(maxfds+1, &fds, NULL, NULL, NULL) < 0) {
            perror("select");
        } else {
            for (p = head; p; p = p->next) { if (FD_ISSET(p->fd, &fds)) { break; } }

            printf("~Attempting to launch~\n");
            
            // process an exisiting client
            if (p) { manage_client(p); }

            // process a new client connection
            if (FD_ISSET(sock, &fds)) { new_connection(sock, client_addr, client_len); }
        }
    }
    return(0);
}
