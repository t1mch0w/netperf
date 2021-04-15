typedef struct client_attrs_struct {
  int id;
  char* ip;
  int port; 
  int duration;
  unsigned long nbytes;
  unsigned long thrput;
} client_attrs_t;

static int server_port = 9030;
static char reply[] = "1";

unsigned long read_tsc(void);
void tcp_read(int sock, short event, void* arg);
void tcp_accept(evutil_socket_t listener, short event, void *arg);
int tcp_server_start(int server_port);
int udp_server_start(int server_port);
void *tcp_client_worker(void* arg);
void *tcp_client_start(int nworker, char* ip, int port, int nbytes, int total_time);
void arg_error(void);
