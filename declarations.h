// Diogo Miguel Henriques Correia (2016219825)
// Henrique José Gouveia Lobo (2020225959)
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SIZE 100
#define MAX_SERVERS 20
#define PIPE_NAME "TASK_PIPE"
#define AVAILABLE 0
#define BUSY 1
#define BLOCKED -1

enum _Performance { STOPPED, NORMAL, HIGH };

pid_t task_managerPID, monitorPID, maintenance_managerPID;
pid_t pids_servers[SIZE];

typedef struct _edge_server_struct {
    char* server_name;
    int cap_vCPU1;
    int cap_vCPU2;
    int tasks_executed;
    int maintenance_operations;
    enum _Performance performance;
} edge_server_struct;

typedef struct node {
    int task_id;
    int nr_instructions;
    int max_time;
    int arrive_time;
    int nr_vcpu;
    struct node* next;
} Node;

typedef struct _config_struct {
    int queue_slots;
    int max_wait;
    int number_edge_servers;
    edge_server_struct* servers[MAX_SERVERS];
    Node* task_queue;
    int nr_tasks_in_queue;
    bool high_performance_flag;
    bool terminate_flag;

    int nr_started_servers;

    int servers_status[MAX_SERVERS][2];

    // stats
    int total_executed_tasks;
    int avg_response_time;
    int unexecuted_tasks;

    pthread_mutexattr_t attrmutex;
    pthread_condattr_t attrcond;

    pthread_cond_t cond_dispatcher;

    pthread_cond_t cond_monitor;
    pthread_mutex_t mutex_monitor;
} config_struct;

typedef struct message_struct {
    long msgtype;
    char text[SIZE];
    int maintenance_time;
} message;

typedef struct vcpu_attributes_struct {
    int capacity;
    int nr_vcpu;
    int nr_server;
} vcpu_att;

// mutexes
sem_t* mutex_log;
sem_t* mutex_servers;

pthread_mutex_t mutex_write;

// estrutura para memoria partilhada
config_struct* config;
int shmid;

// unnamed pipes
int fd_pipe[MAX_SERVERS][2];
fd_set read_set;

int tasks_times[MAX_SERVERS][2];

pthread_mutex_t mutex_tasks[MAX_SERVERS][2];
pthread_t vcpus[MAX_SERVERS][2];

pthread_mutex_t mutex_end_tasks[MAX_SERVERS][2];

// ficheiro de log
FILE* f;

// message queue id
int mqid;

// funcoes
void write_log(char msg[]);
int read_file();
void initiate();
void terminate();
void task_manager();
void* scheduler(void* i);
void* dispatcher(void* i);
void* do_task();
void edge_server(int server_number);
void monitor();
void maintenance_manager();
int number_of_available_vcpus();
void free_servers_list();
void print_servers();
void print_stats();
void print_queue(Node* root);
void sigint_handler();
void sigtstp_handler();

Node* newNode(int t_id, int ni, int t, int arr_time);
Node* peek(Node* head);
void pop(Node** head);
void push(Node** head, Node* temp);
bool isEmpty(Node** head);