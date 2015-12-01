/*------------------------------------------------------
 *
 *  lyrebird.server.c
 *
 *  this file contains the main function that used to build
 *  a server and then assign tasks to client sides.
 *
 *  Project         : LyreBird
 *  Name            : Chong Guo
 *  Student ID      : 301295753
 *  SFU username    : armourg
 *  Lecture section : D1
 *  Instructor      : Brain G.Booth
 *  TA              : Scott Kristjanson
 *
 *  Created by Armour on 25/09/2015
 *  Copyright (c) 2015 Armour. All rights reserved.
 *
 *------------------------------------------------------
 */

#include "lyrebird.server.h"
#include "decrypt.h"
#include "pipe.h"
#include "scheduling.h"
#include "time.h"
#include "memwatch.h"
#include <errno.h>

int main_flag = EXIT_SUCCESS;   /* Used to store return value for main function */
char *enc_txt;                  /* Used to store encrypted file name */
char *dec_txt;                  /* Used to store decrypted file name */
char *out_time;                 /* Used to store output time */
FILE *fcfg, *flog;              /* The file pointer that used to open config file and log file */
    FILE *fuck;

int max_fds;                    /* The max number of file descriptor */
fd_set rfds;                    /* The set of file descriptor */

char host[NI_MAXHOST];                  /* Used to store the return value of getnameinfo function */
struct ifaddrs *ifaddr, *ifa;           /* Used to store the return value of getifaddrs function */

int sockfd, sockfd_new;                         /* Socket file descriptor */
int sockfd_cli[CLIENT_MAXNUM];                  /* The array of client socket file descriptor */
char ipaddr_cli[CLIENT_MAXNUM][NI_MAXHOST];     /* The array of client IP address */
socklen_t addr_len;                             /* The size of sockaddr_in */
struct sockaddr_in serv_addr, cli_addr;         /* Used to store the server and client address information */

char ip_buffer[INET_ADDRSTRLEN];                /* A buffer that used to store client IP address */
int read_flag = 1;              /* This is a flag that means program should read a new line from config file */
int finish_flag = 0;            /* This is a flag that indicate reaching the end of config file */
char *client_ip;                /* A pointer to the IP address char array of one client */
int max_task = 0;               /* This is the number of the decryption tasks that has been assigned */
int cnt_task = 0;               /* This is the number of the decryption tasks that has already finished */
uint32_t read_type;             /* This is the message that read from one socket to client side */

/*
 * Function: Get_time
 * -------------------
 *   This function is used to get current time in specify format
 *
 *   Parameters:
 *      no parameters
 *
 *   Returns:
 *      void
 */

void get_time(void) {
    time_t raw_time;                /* Used to generate output time */
    struct tm *tmp_time;            /* Used to generate output time */
    time(&raw_time);
    tmp_time = localtime(&raw_time);
    strftime(out_time, TIME_MAXLENGTH, "%a %b %d %H:%M:%S %Y", tmp_time);       /* Format time */
}

/*
 * Function: Get_host_by_sockfd
 * -------------------
 *   This function is used to get host ip address through socket file descriptor
 *
 *   Parameters:
 *      sockfd: a file descriptor
 *
 *   Returns:
 *      a pointer to a host ip address
 */

char *get_host_by_sockfd(int sockfd) {
    int i;
    for (i = 0; i < CLIENT_MAXNUM; ++i) {
        if (sockfd_cli[i] == sockfd)
            return ipaddr_cli[i];
    }
    return NULL;
}

/*
 * Function: Clean_up
 * -------------------
 *   This function is used to free memory and close file pointer before program exit
 *
 *   Parameters:
 *      no parameters
 *
 *   Returns:
 *      void
 */

void clean_up(int step) {
    if (step >= CLEAN_TO_TXT) free(enc_txt);
    if (step >= CLEAN_TO_TXT) free(dec_txt);
    if (step >= CLEAN_TO_TIME) free(out_time);
    if (step >= CLEAN_TO_CONFIG) fclose(fcfg);
    if (step >= CLEAN_TO_LOG) fclose(flog);
    if (step >= CLEAN_TO_IFADDR) freeifaddrs(ifaddr);
    if (step >= CLEAN_TO_SOCKET) close(sockfd);
}


void get_ipaddr(void) {
    int i;

    if (getifaddrs(&ifaddr) == -1) {        /* Get the ip address of this machine and check if it failed */
        get_time();
        printf("[%s] (Process ID #%d) ERROR: Server can not get the interface address of this machine!\n", out_time, getpid());
        clean_up(CLEAN_TO_LOG);
        exit(EXIT_FAILURE);
    }

    addr_len = sizeof(struct sockaddr_in);

    for (ifa = ifaddr, i = 0; ifa != NULL; ifa = ifa->ifa_next, i++) {          /* Iterate each interface addresses and get the IP address */
        if (ifa->ifa_addr == NULL)
            continue;

        if (ifa->ifa_addr->sa_family != AF_INET)                                                  /* If it is not the IPv4 interface */
            continue;

        if (getnameinfo(ifa->ifa_addr, addr_len, host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) != 0) {         /* If can not get the address information of interface address */
            get_time();
            printf("[%s] (Process ID #%d) ERROR: Server can not get the address information of interface address!\n", out_time, getpid());
            clean_up(CLEAN_TO_IFADDR);
            exit(EXIT_FAILURE);
        }

        if (strcmp(host, "0.0.0.0") == 0 || strcmp(host, "127.0.0.1") == 0)     /* Filter the special ip address "0.0.0.0" and "127.0.0.1" */
            continue;

        break;                                                                  /* If found a avaliable ip address, then we break */
    }
}

void create_socket() {
    int opt = TRUE;                             /* Used when set socket options */

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {                       /* Create socket in server */
        get_time();
        printf("[%s] (Process ID #%d) ERROR: Server can not create a new socket!\n", out_time, getpid());
        clean_up(CLEAN_TO_IFADDR);
        exit(EXIT_FAILURE);
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0) {      /* Set socket option to reuseable address (not necessary but good) */
        get_time();
        printf("[%s] (Process ID #%d) ERROR: Server can not set socket option!\n", out_time, getpid());
        clean_up(CLEAN_TO_SOCKET);
        exit(EXIT_FAILURE);
    }
}

void bind_socket(void) {
    memset(&serv_addr, 0, addr_len);                                            /* Initialize server address config */
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(0);

    if (inet_pton(AF_INET, host, &(serv_addr.sin_addr)) <= 0) {
        get_time();
        printf("[%s] (Process ID #%d) ERROR: Host in inet_pton function is not a valid IP address!\n", out_time, getpid());
        clean_up(CLEAN_TO_SOCKET);
        exit(EXIT_FAILURE);
    }

    if (bind(sockfd, (struct sockaddr *)&serv_addr, addr_len) < 0) {            /* Bind the server socket */
        get_time();
        printf("[%s] (Process ID #%d) ERROR: Server bind socket failed!\n", out_time, getpid());
        clean_up(CLEAN_TO_SOCKET);
        exit(EXIT_FAILURE);
    }
}

void listen_socket(void) {
    if (listen(sockfd, CLIENT_MAXNUM) < 0) {                                       /* Listen at the server socket */
        get_time();
        printf("[%s] (Process ID #%d) ERROR: Server listen at socket failed!\n", out_time, getpid());
        clean_up(CLEAN_TO_SOCKET);
        exit(EXIT_FAILURE);
    }
}

void print_server_info(void) {
    if (getsockname(sockfd, (struct sockaddr *)&serv_addr, &addr_len) < 0) {            /* Find out the port number that used for lyrebird in server machine */
        get_time();
        printf("[%s] (Process ID #%d) ERROR: Server get socket name failed!\n", out_time, getpid());
        clean_up(CLEAN_TO_SOCKET);
        exit(EXIT_FAILURE);
    }
    get_time();                                                                         /* Print out the server information */
    printf("[%s] lyrebird.server: PID %d on host %s, port %d\n", out_time, getpid(), host, ntohs(serv_addr.sin_port));
}

void init_cli_sock(void) {
    int i;
    for (i = 0; i < CLIENT_MAXNUM; ++i) {
        sockfd_cli[i] = 0;
    }
}

void init_select(void) {
    int i;
    FD_ZERO(&rfds);
    FD_SET(sockfd, &rfds);
    max_fds = sockfd;
    for (i = 0; i < CLIENT_MAXNUM; i++) {
        if (sockfd_cli[i] > 0)
            FD_SET(sockfd_cli[i], &rfds);
        if (sockfd_cli[i] > max_fds)
            max_fds = sockfd_cli[i];
    }
}

int select_func(void) {
    if (select(max_fds + 1, &rfds, NULL, NULL, NULL) == -1) {
        get_time();
        fprintf(flog, "[%s] (Process ID #%d) ERROR: Server failed when run select function!\n", out_time, getpid());
        main_flag = EXIT_FAILURE;
        return -1;
    }
    return 0;
}

uint32_t read_client_msg(int sockfd) {
    uint32_t msg;
    read(sockfd, &msg, sizeof(uint32_t));
    return ntohl(msg);
}

int accept_new_cli(void) {
    if ((sockfd_new = accept(sockfd, (struct sockaddr *)&cli_addr, &addr_len)) < 0) {       /* Accept new connections from client machine */
        get_time();
        fprintf(flog, "[%s] (Process ID #%d) ERROR: Server accept client socket failed!\n", out_time, getpid());
        main_flag = EXIT_FAILURE;
        return -1;
    }
    return 0;
}

int check_connect(void) {
    if (read_client_msg(sockfd_new) != CONNECT_MSG) {
        get_time();
        fprintf(flog, "[%s] (Process ID #%d) ERROR: Server accept client socket failed!\n", out_time, getpid());
        main_flag = EXIT_FAILURE;
        return -1;
    }
    return 0;
}

int print_client_info(void) {
    if (inet_ntop(AF_INET, &cli_addr.sin_addr, ip_buffer, sizeof(ip_buffer)) == NULL) {
        get_time();
        fprintf(flog, "[%s] (Process ID #%d) ERROR: Server get client ip address by inet_ntop failed!\n", out_time, getpid());
        main_flag = EXIT_FAILURE;
        return -1;
    }
    get_time();
    fprintf(flog, "[%s] Successfully connected to lyrebird client %s.\n", out_time, ip_buffer);
    return 0;
}

void store_client_ip(void) {
    int i;
    for (i = 0; i < CLIENT_MAXNUM; ++i) {
        if (sockfd_cli[i] == 0) {
            sockfd_cli[i] = sockfd_new;
            printf("new client in sokcet %d array %d\n", sockfd_new, i);
            memset(ipaddr_cli[i], 0, sizeof(ipaddr_cli[i]));
            strcpy(ipaddr_cli[i], ip_buffer);
            break;
        }
    }
}

void handle_success(int sock_num) {
    uint32_t read_pid;                   /* This is the pid number that read from one socket to client side */
    char read_buffer[ERROR_MAXLENGTH];          /* This is the buffer that we used to read message from socket */
    read(sock_num, read_buffer, sizeof(char) * FILE_MAXLENGTH);
    fprintf(fuck, "%s\n", read_buffer);
    read(sock_num, &read_pid, sizeof(uint32_t));
    fprintf(fuck, "%u\n", ntohl(read_pid));
    read_pid = ntohl(read_pid);
    get_time();
    fprintf(flog, "[%s] The lyrebird client %s has successfully decrypted %s in process %u.\n", out_time, client_ip, read_buffer, read_pid);
    cnt_task++;
    printf("%u cnt: %d max: %d\n", read_type, cnt_task, max_task);
}

void handle_dispatch(int sock_num) {
    uint32_t mark = CLIENT_WORK_MSG;
    mark = htonl(mark);
    write(sock_num, &mark, sizeof(uint32_t));
    write(sock_num, enc_txt, sizeof(char) * FILE_MAXLENGTH);
    write(sock_num, dec_txt, sizeof(char) * FILE_MAXLENGTH);
    get_time();
    fprintf(flog, "[%s] The lyrebird client %s has been given the task of decrypting %s.\n", out_time, client_ip, enc_txt);
    max_task++;
    printf("%u cnt: %d max: %d\n", read_type, cnt_task, max_task);
}

void handle_failure(int sock_num) {
    char read_buffer[ERROR_MAXLENGTH];          /* This is the buffer that we used to read message from socket */
    read(sock_num, read_buffer, sizeof(char) * ERROR_MAXLENGTH);
    fprintf(fuck, "%s\n", read_buffer);
    get_time();
    fprintf(flog, "[%s] The lyrebird client %s has encountered an error: %s", out_time, client_ip, read_buffer);
    cnt_task++;
    printf("%u cnt: %d max: %d\n", read_type, cnt_task, max_task);
}

void handle_client_msg(int sock_num, uint32_t read_type){
    switch (read_type) {
        case SUCCESS_MSG:
            handle_success(sock_num);
        case FAILURE_MSG:
            if (read_type != SUCCESS_MSG)
                handle_failure(sock_num);
        case DISPATCH_MSG:
            if (finish_flag) break;
            handle_dispatch(sock_num);
            read_flag = 1;
            break;
        default:
            get_time();
            fprintf(flog, "[%s] (Process ID #%d) ERROR: Server failed when checking message type from client machine!\n", out_time, getpid());
            main_flag = EXIT_FAILURE;
            break;
    }
}

int ask_clients_quit(void) {
    int i;
    int remained_cli = 0;
    for (i = 0; i < CLIENT_MAXNUM; ++i) {
        if (sockfd_cli[i] > 0) {
            uint32_t mark = CLIENT_EXIT_MSG;
            mark = htonl(mark);
            write(sockfd_cli[i], &mark, sizeof(uint32_t));
            remained_cli++;
        }
    }
    return remained_cli;
}

void wait_clients_quit(int remained_cli) {
    int i, j;
    uint32_t read_type;
    while (remained_cli) {
        init_select();
        if (select_func() == -1) break;
        for (i = 0; i < max_fds + 1; i++) {
            if (FD_ISSET(i, &rfds)) {
                client_ip = get_host_by_sockfd(i);
                printf("Wait for %s socket %d\n", client_ip, i);
                read_type = read_client_msg(i);
                printf("RMSG %u\n", read_type);
                if (read_type == DISCONNECT_SUCC_MSG || read_type == DISCONNECT_FAIL_MSG) {
                    get_time();
                    if (read_type == DISCONNECT_SUCC_MSG)
                        fprintf(flog, "[%s] lyrebird client %s has disconnected expectedly.\n", out_time, client_ip);
                    if (read_type == DISCONNECT_FAIL_MSG)
                        fprintf(flog, "[%s] lyrebird client %s has disconnected unexpectedly.\n", out_time, client_ip);
                    for (j = 0; j < CLIENT_MAXNUM; ++j) {
                        if (sockfd_cli[j] == i) {
                            close(i);
                            sockfd_cli[j] = 0;
                            memset(ipaddr_cli[j], 0, sizeof(ipaddr_cli[j]));
                            break;
                        }
                    }
                    remained_cli--;
                    printf("Remained: %d\n", remained_cli);
                } else {
                    get_time();
                    fprintf(flog, "[%s] (Process ID #%d) ERROR: Server failed when checking message type from client machine!\n", out_time, getpid());
                    //main_flag = EXIT_FAILURE;
                }
                break;
            }
        }
    }
}

void quit_server(void) {
    get_time();                                                 /* Print out the server information */
    printf("[%s] lyrebird.server: PID %d completed its tasks and is exiting successfully.\n", out_time, getpid());
    clean_up(CLEAN_ALL);                                                /* Always remember to free all and close file pointer! */
}

void check_par(int argc, char *argv[]) {
    if (argc != 3) {                        /* Check if the arguments number is right or not */
        get_time();
        printf("[%s] (Process ID #%d) Arguments number is not right! Usage: %s <config_file> <log_file>\n", out_time, getpid(), argv[0]);
        clean_up(CLEAN_TO_TIME);
        exit(EXIT_FAILURE);
    }
}

void open_config(char *argv[]) {
    fcfg = fopen(argv[1], "r+");            /* Input config file */
    if (fcfg == NULL) {                     /* Check if the config file is exist or not */
        get_time();
        printf("[%s] (Process ID #%d) ERROR: Config file %s not exist!\n", out_time, getpid(), argv[1]);
        clean_up(CLEAN_TO_TIME);
        exit(EXIT_FAILURE);
    }
}

void open_log(char *argv[]) {
    flog = fopen(argv[2], "w");             /* Output log file */
    if (flog == NULL) {                     /* Check if the log file can be open or not */
        get_time();
        printf("[%s] (Process ID #%d) ERROR: Log file %s can not open!\n", out_time, getpid(), argv[2]);
        clean_up(CLEAN_TO_CONFIG);
        exit(EXIT_FAILURE);
    }

    fuck = fopen("fuck.txt", "w");             /* Output log file */
}

/*
 * Function: Main
 * -------------------
 *   Main function of lyrebird
 *
 *   Parameters:
 *      argc: the number of parameters in main function
 *      argv[]: the array of parameters in main function
 *
 *   Returns:
 *      void
 */

int main(int argc, char *argv[]) {
    int i;

    out_time = (char *)malloc(sizeof(char) * TIME_MAXLENGTH);
    enc_txt = (char *)malloc(sizeof(char) * FILE_MAXLENGTH);
    dec_txt = (char *)malloc(sizeof(char) * FILE_MAXLENGTH);

    check_par(argc, argv);
    open_config(argv);
    open_log(argv);

    get_ipaddr();

    create_socket();
    bind_socket();
    listen_socket();

    print_server_info();

    init_cli_sock();

    while (TRUE) {                /* Read until end of file */
        if (read_flag) {
            if (fscanf(fcfg, "%s", enc_txt) == EOF) {
                finish_flag = 1;
            } else {
                fscanf(fcfg, "%s", dec_txt);
            }
        }
        read_flag = 0;

        if (finish_flag && cnt_task == max_task) break;

        init_select();
        if (select_func() == -1) break;

        for (i = 0; i < max_fds + 1; i++) {
            if (FD_ISSET(i, &rfds)) {
                if (i == sockfd) {                              /* If have a new client connect */
                    printf("New client!!\n");
                    if (accept_new_cli() == -1) break;
                    if (check_connect() == -1) break;
                    if (print_client_info() == -1) break;
                    store_client_ip();
                    break;
                } else {
                    uint32_t msg = read_client_msg(i);
                    printf("Client msg : %u!!\n", msg);
                    client_ip = get_host_by_sockfd(i);
                    handle_client_msg(i, msg);
                    break;
                }
            }
            if (main_flag == EXIT_FAILURE) break;
        }
        if (main_flag == EXIT_FAILURE) break;
    }

    int remained_cli = ask_clients_quit();
    printf("Total Remained Client: %d\n", remained_cli);
    wait_clients_quit(remained_cli);
    quit_server();

    return main_flag;
}