#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/sysinfo.h>
#include <fcntl.h>
#include <signal.h>

#include <event.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/buffer.h>

#include "netperf.h"

unsigned long read_tsc(void) {
   unsigned int lo, hi;
   __asm__ __volatile__ ("rdtscp" : "=a"(lo), "=d"(hi));
   return ((unsigned long)lo) | (((unsigned long)hi) << 32);
}

static int setnonblock(int fd) {
  int flags;

  flags = fcntl(fd, F_GETFL);
  if (flags < 0) return flags;
  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags) < 0) return -1;
  return 0;
}

void* tcp_server_thread(void* arg) {
  int listener;
  int reuseaddr_on;

  signal(SIGPIPE, SIG_IGN);
  listener = socket(AF_INET, SOCK_STREAM, 0);
  if(listener == -1 ) {
    perror("tcp_server_init failed.\n");
    exit(-1);
  }

  struct sockaddr_in sin;
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = inet_addr("10.10.1.1");
  //sin.sin_addr.s_addr = 0; 
  sin.sin_port = htons((int)(long)arg);

  if(bind(listener, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
    perror("binding failed.\n");
    exit(-1);
  }

  if(listen(listener, 64) < 0) {
    perror("listening failed.\n");
    exit(-1);
  }

  reuseaddr_on = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, sizeof(reuseaddr_on));
  setnonblock(listener);

  struct event_base* base = event_base_new();
  struct event* ev_listen = event_new(base, listener, EV_READ | EV_PERSIST, tcp_accept, (void*) base);
  event_base_set(base, ev_listen);
  event_add(ev_listen, NULL);

  event_base_dispatch(base);
}

void tcp_read(int fd, short event, void* arg) {
  int nchar;
  char buffer[1048576];
  //printf("tcp_read is called\n");
  nchar = read(fd, buffer, 1048576);
  //nchar = write(sock, reply, strlen(reply));

  nchar = write(fd, reply, strlen(reply));
  //printf("send %d bytes\n", nchar);
}

static void udp_read(const int fd, short int which, void *arg) {
  int nchar;
  char buf[1048576];

  struct sockaddr_in server_sin;
  socklen_t server_sz = sizeof(server_sin);

  nchar = recvfrom(fd, &buf, sizeof(buf) - 1, 0, (struct sockaddr *) &server_sin, &server_sz);

  nchar = sendto(fd, reply, strlen(reply), 0, (struct sockaddr *) &server_sin, server_sz);
}

void tcp_accept(int listener, short ev, void *arg) {
  int fd; 
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);

  fd = accept(listener, (struct sockaddr *)&client_addr, &client_len); 
  if (fd < 0) { 
    perror("accept failed.\n"); 
    exit(-1); 
  } 

  //if (fd > FD_SETSIZE) { 
  //  perror("fd > FD_SETSIZE\n"); 
  //  exit(-1); 
  //} 

  printf("ACCEPT: fd = %u\n", fd); 

  struct event_base* base = (struct event_base*)arg;
  struct event* read_ev = (struct event*)malloc(sizeof(struct event));

  event_set(read_ev, fd, EV_READ|EV_PERSIST, tcp_read, NULL);
  event_base_set(base, read_ev);
  event_add(read_ev, NULL);
}

int tcp_server_start(int server_port) {
  int ncores = get_nprocs();
  int i = 0;
  pthread_t threads[ncores];
  for (i = 0; i < ncores; i++) {
    pthread_create(&threads[i], NULL, tcp_server_thread, (void*)(long)(server_port + i));
  }

  for (i = 0; i < ncores; i++) {
    pthread_join(threads[i], NULL);
  }
}

void *udp_server_thread(void* arg) {
  int sock;
  struct event *udp_event = (struct event*)malloc(sizeof(struct event));
  int server_port = (int)(long)(arg);

  sock = socket(AF_INET, SOCK_DGRAM, 0);

  struct sockaddr_in sin;
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = inet_addr("10.10.1.1");
  //sin.sin_addr.s_addr = 0; 
  sin.sin_port = htons(server_port);

  if (bind(sock, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
    perror("binding failed.\n");
    exit(-1);
  }

  struct event_base* base = event_base_new();
  event_set(udp_event, sock, EV_READ|EV_PERSIST, udp_read, NULL);
  event_base_set(base, udp_event);
  event_add(udp_event, NULL);

  event_base_dispatch(base);
}

int udp_server_start(int server_port) {
  int ncores = get_nprocs();
  int i = 0;
  pthread_t threads[ncores];
  for (i = 0; i < ncores; i++) {
    pthread_create(&threads[i], NULL, udp_server_thread, (void*)(long)(server_port + i));
  }

  for (i = 0; i < ncores; i++) {
    pthread_join(threads[i], NULL);
  }
}

void *udp_client_worker(void* arg) {
  int sockfd;
  int nchar;
  int i;
  client_attrs_t *client_attr = (client_attrs_t*) arg;
  char *sbuf = (char*)malloc(sizeof(char) * (client_attr->nbytes + 1));
  char rbuf[256];
  struct sockaddr_in server_addr;
  int server_len;

  //Record the performance
  unsigned long stime, etime, throughput, start_time, end_time;
  unsigned long *lats;
  lats = (unsigned long*)malloc(sizeof(unsigned long) * 1e10);

  printf("client %d starts\n", client_attr->id);
  memset(&server_addr,0,sizeof(server_addr));
  server_addr.sin_family=AF_INET;
  server_addr.sin_addr.s_addr=inet_addr(client_attr->ip);
  server_addr.sin_port=htons(client_attr->port);
  server_len = sizeof(server_addr);
  
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);

  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 500;
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  //Init the payload 
  memset(sbuf, 0, sizeof(sbuf));
  memset(rbuf, 0, sizeof(rbuf));
  for (i = 0; i < client_attr->nbytes; i++) {
    sbuf[i] = 'a';
  }

  start_time = read_tsc(); 
  start_time += 5e9 * 2.6; 
  end_time = start_time + client_attr->duration * 1e9 * 2.6; 

  while(1) {
    stime = read_tsc();
    //printf("stime = %ld, start_time = %ld, end_time = %ld\n", stime, start_time, end_time);
    nchar = sendto(sockfd, sbuf, strlen(sbuf), 0, (struct sockaddr *)&server_addr, server_len);
    //printf("Send %d byte to Server\n", nchar);
    nchar = recvfrom(sockfd, rbuf, 256, 0, (struct sockaddr *)&server_addr, &server_len);
    //printf("Recv %d byte from Server: %s\n", nchar, rbuf);
    etime = read_tsc();

    //printf("stime =  %ld, end_time = %ld\n", stime, end_time);
    if (stime > end_time) {
      break;
    }

    if (stime > start_time) {
      //lats[(etime - stime)]++;
      ++throughput;
    }
  }

  //print out the latency
  //long j;
  //for (j = 0; j < 1e10; j++) {
  //  if (lats[j] == 0) continue;
  //  printf("%ld %ld\n", j, lats[j]);
  //}
  client_attr->thrput = throughput;
}

void *tcp_client_worker(void* arg) {
  int conndfd;
  int nchar;
  int i;
  client_attrs_t *client_attr = (client_attrs_t*) arg;
  char *sbuf = (char*)malloc(sizeof(char) * (client_attr->nbytes + 1));
  char rbuf[256];
  struct sockaddr_in server_addr;

  //Record the performance
  unsigned long stime, etime, throughput, start_time, end_time;
  unsigned long *lats;
  lats = (unsigned long*)malloc(sizeof(unsigned long) * 1e10);

  printf("client %d starts\n", client_attr->id);
  memset(&server_addr,0,sizeof(server_addr));
  server_addr.sin_family=AF_INET;
  server_addr.sin_addr.s_addr=inet_addr(client_attr->ip);
  server_addr.sin_port=htons(client_attr->port);
  
  if((conndfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("creating socket failed\n");
    exit(-1);
  }

   if(connect(conndfd, (struct sockaddr*)&server_addr,sizeof(server_addr)) == -1) {
    perror("connect failed\n");
    exit(-1);
  }

  //Init the payload 
  memset(sbuf, 0, sizeof(sbuf));
  memset(rbuf, 0, sizeof(rbuf));
  for (i = 0; i < client_attr->nbytes; i++) {
    sbuf[i] = 'a';
  }

  start_time = read_tsc(); 
  start_time += 5e9 * 2.6; 
  end_time = start_time + client_attr->duration * 1e9 * 2.6; 

  while(1) {
    stime = read_tsc();
    //printf("stime = %ld, start_time = %ld, end_time = %ld\n", stime, start_time, end_time);
    nchar = write(conndfd, sbuf, strlen(sbuf));
    //printf("Send %d byte to Server\n", nchar);
    nchar = read(conndfd, rbuf, 256);
    //printf("Recv %d byte from Server: %s\n", nchar, rbuf);
    etime = read_tsc();

    if (stime > end_time) {
      break;
    }

    if (stime > start_time) {
      //lats[(etime - stime)]++;
      ++throughput;
    }
  }

  //print out the latency
  //long j;
  //for (j = 0; j < 1e10; j++) {
  //  if (lats[j] == 0) continue;
  //  printf("%ld %ld\n", j, lats[j]);
  //}
  client_attr->thrput = throughput;
}

void *udp_client_start(int nworker, char* ip, int port, int nbytes, int duration) {
  int i = 0;
  unsigned long total_throughput = 0;
  int ncores = get_nprocs();
  pthread_t threads[nworker]; 
  client_attrs_t *client_attrs;
  client_attrs = (client_attrs_t*)malloc(nworker * sizeof(client_attrs_t));

  for (i = 0; i < nworker; i++) {
    client_attrs[i].id = i;
    client_attrs[i].ip = ip;
    client_attrs[i].port = port + i % ncores;
    client_attrs[i].nbytes = nbytes;
    client_attrs[i].duration = duration;
    pthread_create(&threads[i], NULL, udp_client_worker, (void*)(&client_attrs[i]));
  }

  for (i = 0; i < nworker; i++) {
    pthread_join(threads[i], NULL);
  }

  for (i = 0; i < nworker; i++) {
    total_throughput += client_attrs[i].thrput;
  }

  printf("total_throughput = %f\n", (float)total_throughput / duration);
}

void *tcp_client_start(int nworker, char* ip, int port, int nbytes, int duration) {
  int i = 0;
  int ncores = get_nprocs();
  unsigned long total_throughput = 0;
  pthread_t threads[nworker]; 
  client_attrs_t *client_attrs;
  client_attrs = (client_attrs_t*)malloc(nworker * sizeof(client_attrs_t));
  for (i = 0; i < nworker; i++) {
    client_attrs[i].id = i;
    client_attrs[i].ip = ip;
    client_attrs[i].port = port + i % ncores;
    client_attrs[i].nbytes = nbytes;
    client_attrs[i].duration = duration;
    pthread_create(&threads[i], NULL, tcp_client_worker, (void*)(&client_attrs[i]));
  }

  for (i = 0; i < nworker; i++) {
    pthread_join(threads[i], NULL);
  }

  for (i = 0; i < nworker; i++) {
    total_throughput += client_attrs[i].thrput;
  }

  printf("total_throughput = %f\n", (float)total_throughput / duration);
}

void arg_error(void) {
  fprintf(stderr, "Usage: [-s]/[-c] [-t]/[-u] [-h server_ip] [-n num_of_clients] [-p packet_length] [-d duration]\n");
  fprintf(stderr, "Usage: s/c: server/client, t/u: tcp/udp, h: server ip address, n: number of clients, p: packet length, d: duration\n");
  exit(-1);
}

int main(int argc, char *argv[])
{
  int opt;
  int server = 0;
  int client = 0;
  int udp = 0;
  int tcp = 0;
  int nworker = 1;
  int nbytes = 1;
  int duration = 30;
  char* host;

  while ((opt = getopt(argc, argv, "csuth:n:p:d:")) != -1) {
    switch (opt) {
      case 's':
        server = 1;
        break;
      case 'c':
        client = 1;
        break;
      case 't':
        tcp = 1;
        break;
      case 'u':
        udp = 1;
        break;
      case 'h':
        host = optarg;
        printf("The server ip address: %s\n", host);
        break;
      case 'n':
        nworker = atoi(optarg);
        break;
      case 'p':
        nbytes = atoi(optarg);
        break;
      case 'd':
        duration = atoi(optarg);
	break;
      default:
        arg_error();
    }
  }

  if (server == 0 && client == 0) {
    arg_error();
  }

  if (tcp == 1) {
    if (server == 1) {
      tcp_server_start(server_port);
    }
    else if (client == 1) {
      tcp_client_start(nworker, host, server_port, nbytes, duration);
    }
  }
  else if (udp == 1) {
    if (server == 1) {
      udp_server_start(server_port);
    }
    else if (client == 1) {
      udp_client_start(nworker, host, server_port, nbytes, duration);
    }
  }
}
