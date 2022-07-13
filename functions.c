// Diogo Miguel Henriques Correia (2016219825)
// Henrique José Gouveia Lobo (2020225959)
#include "declarations.h"

void write_log(char mensagem[]) {  //---------------------------------------
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    if (localtime(&t) == NULL) {
        printf("Local time is null\n");
        exit(0);
    }

    sem_wait(mutex_log);
    if (f != NULL) {
        printf("%d:%d:%d %s\n", tm->tm_hour, tm->tm_min, tm->tm_sec, mensagem);
        fprintf(f, "%d:%d:%d %s\n", tm->tm_hour, tm->tm_min, tm->tm_sec,
                mensagem);
    }

    sem_post(mutex_log);
}

int read_file(char* file_name) {
    char* aux;
    long val1, val2;
    FILE* fich;
    char* token;
    char line[SIZE];
    int line_counter = 0;
    int i = 0;
    edge_server_struct* server_temp;
    if ((fich = fopen(file_name, "r")) == NULL) {
        char str[SIZE];
        str[0] = 0;
        strcat(str, "Error! File ");
        strcat(str, file_name);
        strcat(str, " not found");
        write_log(str);
        return -1;
    }

    pthread_mutex_lock(&mutex_write);
    while (fgets(line, SIZE, fich) != NULL) {
        if (line_counter == 0) {
            config->queue_slots = strtol(line, &aux, 10);
        } else if (line_counter == 1) {
            config->max_wait = strtol(line, &aux, 10);
        } else if (line_counter == 2) {
            config->number_edge_servers = strtol(line, &aux, 10);
            if (config->number_edge_servers < 2) {
                write_log("Error! EDGE_SERVER_NUMBER must be >=2");
                fclose(fich);
                return -1;
            } else {
                // ler linhas com definicoes do servidor
                while (fgets(line, SIZE, fich) != NULL) {
                    server_temp =
                        (edge_server_struct*)malloc(sizeof(edge_server_struct));
                    server_temp->server_name =
                        (char*)malloc(sizeof(char) * SIZE);

                    // nome do servidor
                    token = strtok(line, ",");
                    strcpy(server_temp->server_name, token);

                    // valor da primeira vcpu
                    token = strtok(NULL, ",");
                    val1 = strtol(token, &aux, 10);
                    server_temp->cap_vCPU1 = val1;

                    // valor da segunda vcpu
                    token = strtok(NULL, ",");
                    val2 = strtol(token, &aux, 10);

                    server_temp->cap_vCPU1 = val1;
                    server_temp->cap_vCPU2 = val2;
                    server_temp->performance = NORMAL;
                    server_temp->maintenance_operations = 0;
                    server_temp->tasks_executed = 0;

                    config->servers[i] = server_temp;
                    i++;
                }
            }
            fclose(fich);
            // inicializar matriz de status
            for (int i = 0; i < config->number_edge_servers; i++) {
                config->servers_status[i][0] = AVAILABLE;
                config->servers_status[i][1] = BLOCKED;
            }
            // sem_post(mutex_write);
            pthread_mutex_unlock(&mutex_write);
            return 0;
        }
        line_counter++;
    }

    pthread_mutex_unlock(&mutex_write);
    return -1;
}

void initiate() {
    // apagar ficheiro de log se existir
    remove("log.txt");

    // abrir ficheiro de log para escrita
    f = fopen("log.txt", "a");

    // criar memoria partilhada
    if ((shmid = shmget(IPC_PRIVATE, sizeof(config_struct), IPC_CREAT | 0700)) <
        0) {
        perror("Error in shmget with IPC_CREAT\n");
        exit(1);
    }

    // fazer attach da memoria partilhada
    if ((config = (config_struct*)shmat(shmid, NULL, 0)) ==
        (config_struct*)-1) {
        perror("Shmat error.\n");
        exit(1);
    }

    // Criar o named pipe se ainda nao existir
    if ((mkfifo(PIPE_NAME, O_CREAT | O_EXCL | 0600) < 0) && (errno != EEXIST)) {
        perror("Cannot create pipe: ");
        exit(1);
    }

    // inicializar atributo dos mutex e variavel de condicao
    pthread_mutexattr_init(&config->attrmutex);
    pthread_mutexattr_setpshared(&config->attrmutex, PTHREAD_PROCESS_SHARED);

    pthread_condattr_init(&config->attrcond);
    pthread_condattr_setpshared(&config->attrcond, PTHREAD_PROCESS_SHARED);

    // inicializar semaforos
    sem_unlink("MUTEX_LOG");
    mutex_log = sem_open("MUTEX_LOG", O_CREAT | O_EXCL, 0700, 1);

    sem_unlink("MUTEX_SERVERS");
    mutex_servers = sem_open("MUTEX_SERVERS", O_CREAT | O_EXCL, 0700, 0);

    if (pthread_mutex_init(&mutex_write, &config->attrmutex) != 0) {
        perror("Error in mutex init\n");
        exit(1);
    }

    for (int i = 0; i < MAX_SERVERS; i++) {
        // inicializar e dar lock aos mutexes
        pthread_mutex_init(&mutex_tasks[i][0], &config->attrmutex);
        pthread_mutex_init(&mutex_tasks[i][1], &config->attrmutex);
        pthread_mutex_lock(&mutex_tasks[i][0]);
        pthread_mutex_lock(&mutex_tasks[i][1]);

        pthread_mutex_init(&mutex_end_tasks[i][0], &config->attrmutex);
        pthread_mutex_init(&mutex_end_tasks[i][1], &config->attrmutex);
        pthread_mutex_lock(&mutex_end_tasks[i][0]);
        pthread_mutex_lock(&mutex_end_tasks[i][1]);

        // inicializar variaveis de condicao
        if (pthread_cond_init(&config->cond_dispatcher, &config->attrcond) !=
            0) {
            perror("Error in cond init\n");
            exit(1);
        }

        if (pthread_cond_init(&config->cond_monitor, &config->attrcond) != 0) {
            perror("Error in cond init\n");
            exit(1);
        }

        // inicializar pids dos servers a -1
        pids_servers[i] = -1;
    }

    // iniciar variaveis da shared memory
    config->high_performance_flag = false;
    config->terminate_flag = false;
    config->task_queue = NULL;
    config->nr_tasks_in_queue = 0;
    config->nr_started_servers = 0;
    // STATS
    config->total_executed_tasks = 0;
    config->avg_response_time = 0;
    config->unexecuted_tasks = 0;

    write_log("OFFLOAD SIMULATOR STARTING");
}

void terminate() {
    print_stats();
    write_log("SIMULATOR CLOSING");

    free_servers_list();

    // terminar semaforos
    sem_close(mutex_log);
    sem_unlink("MUTEX_LOG");

    sem_close(mutex_servers);
    sem_unlink("MUTEX_SERVERS");

    pthread_mutex_destroy(&mutex_write);
    pthread_cond_destroy(&config->cond_monitor);
    pthread_cond_destroy(&config->cond_dispatcher);

    for (int i = 0; i < MAX_SERVERS; i++) {
        pthread_mutex_destroy(&mutex_tasks[i][0]);
        pthread_mutex_destroy(&mutex_tasks[i][1]);

        pthread_mutex_destroy(&mutex_end_tasks[i][0]);
        pthread_mutex_destroy(&mutex_end_tasks[i][0]);
    }

    // destruir atributos dos mutex e da condvar
    pthread_mutexattr_destroy(&config->attrmutex);
    pthread_condattr_destroy(&config->attrcond);

    // remover named pipe
    unlink(PIPE_NAME);

    // remover message queue
    msgctl(mqid, IPC_RMID, NULL);

    // terminar memoria partilhada
    shmdt(config);
    shmctl(shmid, IPC_RMID, NULL);
}

void task_manager() {
    int fd;
    Node* temp = NULL;
    Node task_term;
    pthread_t thread_scheduler, thread_dispatcher;
    write_log("PROCESS TASK MANAGER CREATED");

    pthread_mutex_lock(&mutex_write);
    config->terminate_flag = false;
    pthread_mutex_unlock(&mutex_write);

    // abrir named pipe para leitura
    if ((fd = open(PIPE_NAME, O_RDONLY)) < 0) {
        perror("Cannot open pipe for reading\n");
        exit(0);
    }

    // criar unnamed pipes
    for (int i = 0; i < config->number_edge_servers; i++) {
        if (pipe(fd_pipe[i]) < 0) {
            perror("Cannot create unnamed pipe\n");
        }
    }

    // criar edge servers
    for (int i = 0; i < config->number_edge_servers; i++) {
        if (fork() == 0) {
            edge_server(i);
        }
    }

    sem_wait(mutex_servers);

    // thread scheduler
    if (pthread_create(&thread_scheduler, NULL, scheduler, &fd) != 0) {
        write_log("ERROR CREATING SCHEDULER THREAD");
    }
    // thread dispatcher
    if (pthread_create(&thread_dispatcher, NULL, dispatcher, NULL) != 0) {
        write_log("ERROR CREATING DISPATCHER THREAD");
    }

    // fazer join das threads
    pthread_join(thread_scheduler, NULL);
    pthread_cancel(thread_dispatcher);

    task_term.task_id = -1;
    for (int i = 0; i < config->number_edge_servers; i++) {
        write(fd_pipe[i][1], &task_term, sizeof(Node));
    }

    // esperar pelo fim dos processos edge server
    for (int i = 0; i < config->number_edge_servers; i++) {
        wait(NULL);
    }
    
    //calcular tarefas que ficaram por realizar
    temp=config->task_queue;
    while(temp){
      config->unexecuted_tasks++;
      temp=temp->next;
    }

    exit(0);
}

void* scheduler(void* i) {
    int fd = *((int*)i);
    char s[SIZE];
    char msg[SIZE];
    int t_id, instructions, t;
    char* ptr;
    Node* node_aux;
    s[0] = 0;
    int nchars;
    while (1) {
        memset(msg, 0, SIZE);
        memset(s, 0, SIZE);

        if ((nchars = read(fd, s, sizeof(s))) > 0) {
            s[nchars - 1] = '\0';

            if (isdigit(s[0])) {
                // tarefa
                strcpy(msg, "recebeu tarefa:");
                strcat(msg, s);
                strcat(msg, "\n");
                // write_log(msg);
                ptr = strtok(s, ":");
                t_id = atoi(ptr);
                ptr = strtok(NULL, ":");
                instructions = atoi(ptr);
                ptr = strtok(NULL, ":");
                t = atoi(ptr);
                memset(s, 0, SIZE);

                pthread_mutex_lock(&mutex_write);
                if (config->nr_tasks_in_queue >= config->queue_slots) {
                    config->unexecuted_tasks++;
                    sprintf(msg,
                            "SCHEDULER: QUEUE IS FULL, TASK %d DISCARDED\n",
                            t_id);
                    write_log(msg);
                    pthread_mutex_unlock(&mutex_write);
                } else {
                    int arr_time = time(NULL);
                    // criar nova tarefa
                    node_aux = newNode(t_id, instructions, t, arr_time);

                    // inserir tarefa na queue
                    push(&config->task_queue, node_aux);

                    config->nr_tasks_in_queue++;
                    // sem_post(mutex_write);
                    pthread_mutex_unlock(&mutex_write);
                    pthread_cond_signal(&config->cond_dispatcher);
                    // pthread_cond_signal(&config->cond_monitor);
                }

                memset(s, 0, SIZE);
            } else if (strcmp(s, "EXIT") == 0) {
                write_log("COMMAND EXIT RECEIVED");
                pthread_mutex_lock(&mutex_write);
                config->terminate_flag = true;
                pthread_mutex_unlock(&mutex_write);
                break;
            } else if (strcmp(s, "STATS") == 0) {
                write_log("COMMAND STATS RECEIVED");
                print_stats();
            } else {
                strcpy(msg, "WRONG COMMAND =>");
                strcat(msg, s);
                strcat(msg, "\n");
                write_log(msg);
            }
        }
    }
}

void* dispatcher(void* arg) {
    int time_in_queue;
    int i;
    int time_to_execute_task_vcpu1, time_to_execute_task_vcpu2;
    char buffer[SIZE];
    char s[SIZE];
    Node* node_temp = NULL;

    while (1) {
        pthread_mutex_lock(&mutex_write);

        // thread espera enquanto a fila estiver vazia ou enquanto nao houver
        // vcpus disponiveis
        while (isEmpty(&config->task_queue) ||
               number_of_available_vcpus() == 0) {
            pthread_cond_wait(&config->cond_dispatcher, &mutex_write);
        }

        if (config->terminate_flag) {
            break;
        }

        node_temp = NULL;
        memset(s, 0, SIZE);

        // ve topo da fila
        node_temp = peek(config->task_queue);

        // retirar tarefa da fila
        pop(&config->task_queue);
        config->nr_tasks_in_queue--;
        time_in_queue = time(NULL) - node_temp->arrive_time;
        if (time_in_queue > node_temp->max_time) {
            write_log("DISPATCHER => TASK DISCARDED 1");
            config->unexecuted_tasks++;
            continue;
        }

        // procura uma vCPU que esteja disponivel
        for (i = 0; i < config->number_edge_servers; i++) {
            // se o servidor so tiver 1 vcpu ativo
            if (config->high_performance_flag == false) {
                time_to_execute_task_vcpu1 =
                    (node_temp->nr_instructions * 1000) /
                    (config->servers[i]->cap_vCPU1 * 1000000);
                if (config->servers_status[i][0] == AVAILABLE &&
                    (time_in_queue + time_to_execute_task_vcpu1) <=
                        node_temp->max_time) {
                    memset(buffer, 0, SIZE);
                    node_temp->nr_vcpu = 0;

                    strcat(s, "DISPATCHER: TASK ");
                    sprintf(buffer, "%d", node_temp->task_id);
                    strcat(s, buffer);
                    strcat(s, " SELECTED FOR EXECUTION ON ");
                    strcat(s, config->servers[i]->server_name);

                    write_log(s);

                    // colocar vcpu a ocupada
                    config->servers_status[i][node_temp->nr_vcpu] = BUSY;
                    pthread_mutex_unlock(&mutex_write);

                    write(fd_pipe[i][1], node_temp, sizeof(Node));

                    break;
                }
            }
            // se o servidor tiver os 2 vcpus ativos
            else {
                time_to_execute_task_vcpu1 =
                    (node_temp->nr_instructions * 1000) /
                    (config->servers[i]->cap_vCPU1 * 1000000);
                time_to_execute_task_vcpu2 =
                    (node_temp->nr_instructions * 1000) /
                    (config->servers[i]->cap_vCPU2 * 1000000);
                if (config->servers_status[i][0] == AVAILABLE &&
                    (time_in_queue + time_to_execute_task_vcpu1) <=
                        node_temp->max_time) {
                    memset(buffer, 0, SIZE);
                    node_temp->nr_vcpu = 0;

                    strcat(s, "DISPATCHER: TASK ");
                    sprintf(buffer, "%d", node_temp->task_id);
                    strcat(s, buffer);
                    strcat(s, " SELECTED FOR EXECUTION ON SERVER ");
                    memset(buffer, 0, SIZE);
                    sprintf(buffer, "%d", i);
                    strcat(s, buffer);

                    write_log(s);
                    // colocar vcpu a ocupada
                    config->servers_status[i][node_temp->nr_vcpu] = BUSY;

                    pthread_mutex_unlock(&mutex_write);

                    write(fd_pipe[i][1], &node_temp, sizeof(Node));

                    break;
                } else if (config->servers_status[i][1] == AVAILABLE &&
                           (time_in_queue + time_to_execute_task_vcpu2) <=
                               node_temp->max_time) {
                    memset(buffer, 0, SIZE);
                    node_temp->nr_vcpu = 1;

                    strcat(s, "DISPATCHER: TASK ");
                    sprintf(buffer, "%d", node_temp->task_id);
                    strcat(s, buffer);
                    strcat(s, " SELECTED FOR EXECUTION ON SERVER ");
                    memset(buffer, 0, SIZE);
                    sprintf(buffer, "%d", i);
                    strcat(s, buffer);

                    write_log(s);
                    // colocar vcpu a ocupada
                    config->servers_status[i][node_temp->nr_vcpu] = BUSY;

                    pthread_mutex_unlock(&mutex_write);

                    write(fd_pipe[i][1], &node_temp, sizeof(Node));

                    break;
                }
            }
        }
    }
}

void* do_task(void* arg) {
    vcpu_att v_atts = *((vcpu_att*)arg);
    int t;
    char aux[SIZE];
    char buff[5];
    write_log("vCPU READY TO EXECUTE TASK");

    while (1) {
        pthread_mutex_lock(&mutex_write);

        if (config->terminate_flag) {
            pthread_mutex_unlock(&mutex_write);
            break;
        }
        pthread_mutex_unlock(&mutex_write);
        pthread_mutex_lock(&mutex_tasks[v_atts.nr_server][v_atts.nr_vcpu]);

        // executar a tarefa
        t = tasks_times[v_atts.nr_server][v_atts.nr_vcpu];
        sleep(t);

        pthread_mutex_unlock(
            &mutex_end_tasks[v_atts.nr_server][v_atts.nr_vcpu]);
    }
}

void edge_server(int server_number) {
    int time_to_execute;
    Node node_temp;
    vcpu_att vcpu1_atts, vcpu2_atts;
    pthread_t thread_vcpu1, thread_vcpu2;
    message maintenance_msg;
    char* str_aux;
    char msg[SIZE];
    char buffer[SIZE];
    str_aux = (char*)malloc(sizeof(char) * SIZE);
    str_aux[0] = 0;

    pids_servers[server_number] = getpid();

    // fechar entrada write do pipe
    close(fd_pipe[server_number][1]);

    strcat(str_aux, config->servers[server_number]->server_name);
    strcat(str_aux, " READY");
    write_log(str_aux);

    vcpu1_atts.nr_vcpu = 0;
    vcpu1_atts.nr_server = server_number;
    vcpu1_atts.capacity = config->servers[server_number]->cap_vCPU1;

    vcpu2_atts.nr_vcpu = 1;
    vcpu2_atts.nr_server = server_number;
    vcpu2_atts.capacity = config->servers[server_number]->cap_vCPU2;

    pthread_create(&vcpus[server_number][vcpu1_atts.nr_vcpu], NULL, do_task,
                   &vcpu1_atts);
    pthread_create(&vcpus[server_number][vcpu2_atts.nr_vcpu], NULL, do_task,
                   &vcpu2_atts);

    pthread_mutex_lock(&mutex_write);
    config->nr_started_servers++;
    pthread_mutex_unlock(&mutex_write);

    // se os servidores estiverem todos iniciados, A thread scheduler pode
    // comecar
    if (config->nr_started_servers == config->number_edge_servers) {
        sem_post(mutex_servers);
    }

    while (!config->terminate_flag) {
        read(fd_pipe[server_number][0], &node_temp, sizeof(Node));
        pthread_mutex_lock(&mutex_write);
        memset(msg, 0, SIZE);
        memset(buffer, 0, SIZE);

        // tarefa de terminacao dos servidores
        if (node_temp.task_id == -1) {
            // terminar as thredas vcpus
            // se estiver ocupada, esperar que termine. senao cancelar
            if (config->servers_status[server_number][0] == BUSY) {
                pthread_join(vcpus[server_number][vcpu1_atts.nr_vcpu], NULL);
            } else {
                pthread_cancel(vcpus[server_number][vcpu1_atts.nr_vcpu]);
            }
            // se estiver ocupada, esperar que termine. senao cancelar
            if (config->servers_status[server_number][1] == BUSY) {
                pthread_join(vcpus[server_number][vcpu2_atts.nr_vcpu], NULL);
            } else {
                pthread_cancel(vcpus[server_number][vcpu2_atts.nr_vcpu]);
            }
            break;
        }

        if (node_temp.nr_vcpu == 0) {
            // calcular tempo de execucao
            time_to_execute =
                (node_temp.nr_instructions * 1000) /
                (config->servers[server_number]->cap_vCPU1 * 1000000);

            // colocar tempo de execucao na estrutura
            tasks_times[server_number][node_temp.nr_vcpu] = time_to_execute;

            // fazer unlock para a thread executar
            pthread_mutex_unlock(&mutex_tasks[server_number][0]);

            // fazer lock para saber quando a thread acaba
            pthread_mutex_lock(&mutex_end_tasks[server_number][0]);

            // adicionar estatisticas
            config->servers[server_number]->tasks_executed++;
            config->total_executed_tasks++;

            strcat(msg, config->servers[server_number]->server_name);
            strcat(msg, ": TASK ");
            memset(buffer, 0, SIZE);
            sprintf(buffer, "%d", node_temp.task_id);
            strcat(msg, buffer);
            strcat(msg, " COMPLETED");
            write_log(msg);

            // colocar a vcpu a AVAILABLE
            config->servers_status[server_number][node_temp.nr_vcpu] =
                AVAILABLE;

            pthread_mutex_unlock(&mutex_write);

            pthread_cond_signal(&config->cond_dispatcher);

            // pthread_cond_signal(&config->cond_monitor);

        } else {
            // calcular tempo de execucao
            time_to_execute =
                (node_temp.nr_instructions * 1000) /
                (config->servers[server_number]->cap_vCPU2 * 1000000);

            // colocar tempo de execucao na estrutura
            tasks_times[server_number][1] = time_to_execute;

            // fazer unlock para a thread executar
            pthread_mutex_unlock(&mutex_tasks[server_number][1]);

            // fazer lock para saber quando a thread acaba
            pthread_mutex_lock(&mutex_end_tasks[server_number][1]);

            // adicionar estatisticas
            config->servers[server_number]->tasks_executed++;
            config->total_executed_tasks++;

            strcpy(msg, "SERVER_");
            sprintf(buffer, "%d", server_number);
            strcat(msg, buffer);
            strcat(msg, ": TASK ");
            memset(buffer, 0, SIZE);
            sprintf(buffer, "%d", node_temp.task_id);
            strcat(msg, buffer);
            strcat(msg, " COMPLETED");
            write_log(msg);

            // colocar a vcpu a AVAILABLE
            config->servers_status[server_number][node_temp.nr_vcpu] =
                AVAILABLE;

            pthread_mutex_unlock(&mutex_write);
            pthread_cond_signal(&config->cond_dispatcher);
            // pthread_cond_signal(&config->cond_monitor);
        }

        // verificar se tem operacoes de manutencao
        /*
        if (msgrcv(mqid, &maintenance_msg, sizeof(message), server_number+1,
        IPC_NOWAIT) > 0){
          config->servers_status[server_number][node_temp.nr_vcpu] =
        BLOCKED; config->servers[server_number]->maintenance_operations++;
          memset(buffer, 0, SIZE);
          sprintf(buffer, "%s IN MAINTENANCE",
        config->servers[server_number]->server_name); write_log(buffer);
          sleep(maintenance_msg.maintenance_time);
          //colocar a vcpu a AVAILABLE
        config->servers_status[server_number][node_temp.nr_vcpu] =
        AVAILABLE;
          memset(buffer, 0, SIZE);
          sprintf(buffer, "%s BACK ONLINE", config->servers[server_number]->server_name);
          write_log(buffer);
        }
        */
    }
    exit(0);
}

void monitor() {
    write_log("PROCESS MONITOR CREATED");

    /*
    while(!config->terminate_flag){
      pthread_mutex_lock(&mutex_write);
      while((config->nr_tasks_in_queue/config->queue_slots)<0.8 &&
    (peek(config->task_queue))->max_time <= config->max_wait){
        config->high_performance_flag=false;
        pthread_cond_wait(&config->cond_monitor, &mutex_write);
      }
      config->high_performance_flag=true;
      for(int i=0;i<config->number_edge_servers;i++){
        config->servers_status[i][1]=AVAILABLE;
      }
      write_log("MONITOR ACTIVATED VCPU2");
      pthread_mutex_unlock(&mutex_write);
    }
    */
    exit(0);
}

void maintenance_manager() {
    int time_interval;
    write_log("PROCESS MAINTENANCE MANAGER CREATED");

    message m;
    strcpy(m.text, "manutencao");

    while (!config->terminate_flag) {
        m.msgtype = (rand() % config->number_edge_servers) + 1;
        m.maintenance_time = (rand() % 5) + 1;

        // enviar mensagem de manutencao
        if ((msgsnd(mqid, &m, sizeof(message), 0)) < 0) {
            fprintf(stderr, "msgsnd error: %s\n", strerror(errno));
            exit(1);
        }

        time_interval = (rand() % 5) + 1;
        sleep(time_interval);
    }
    // printf("maintenance manager acabou\n");
    exit(0);
}

int number_of_available_vcpus() {
    int total = 0;
    for (int i = 0; i < config->number_edge_servers; i++) {
        if (config->servers_status[i][0] == AVAILABLE) {
            total++;
        }
        if (config->servers_status[i][1] == AVAILABLE) {
            total++;
        }
    }
    return total;
}

void free_servers_list() {
    int i = 0;
    // sem_wait(mutex_write);
    pthread_mutex_lock(&mutex_write);
    while (config->servers[i] != NULL) {
        free(config->servers[i]->server_name);
        free(config->servers[i]);
        i++;
    }
    pthread_mutex_unlock(&mutex_write);
    // sem_post(mutex_write);
}

void print_servers() {
    int i = 0;
    pthread_mutex_lock(&mutex_write);
    while (config->servers[i] != NULL) {
        printf("Nome:%s\tvCPU1:%d\tvCPU2:%d\tPerformance:%d\n",
               config->servers[i]->server_name, config->servers[i]->cap_vCPU1,
               config->servers[i]->cap_vCPU2, config->servers[i]->performance);
        i++;
    }
    pthread_mutex_unlock(&mutex_write);
}

void print_stats() {
    pthread_mutex_lock(&mutex_write);
    for (int i = 0; i < config->number_edge_servers; i++) {
        printf("%s - %d tasks executed - %d maintenance operations\n",
               config->servers[i]->server_name,
               config->servers[i]->tasks_executed,
               config->servers[i]->maintenance_operations);
    }

    printf("Total executed tasks: %d\n", config->total_executed_tasks);
    printf("Total unexecuted tasks: %d\n", config->unexecuted_tasks);
    pthread_mutex_unlock(&mutex_write);
}

void print_queue(Node* root) {
    Node* temp;
    temp = root;
    // sem_wait(mutex_write);
    pthread_mutex_lock(&mutex_write);
    printf(">>>QUEUE PRINT<<<\n");
    while (temp != NULL) {
        printf("%d-%d-%d, ", temp->task_id, temp->nr_instructions,
               temp->max_time);
        temp = temp->next;
    }
    pthread_mutex_unlock(&mutex_write);
    // sem_post(mutex_write);
    printf("\n>>>QUEUE PRINT ENDED<<<\n");
}

void sigint_handler() {
    int i = 0;
    write_log("SIGNAL SIGINT RECEIVED");
    pthread_mutex_lock(&mutex_write);
    config->terminate_flag = true;
    pthread_mutex_unlock(&mutex_write);

    // matar processos server
    while (pids_servers[i] != -1) {
        kill(pids_servers[i], SIGINT);
        i++;
    }

    for (int i = 0; i < config->number_edge_servers; i++) {
        wait(NULL);
    }

    // matar processos task_manager, maintenance_manager e monitor
    kill(task_managerPID, SIGINT);
    wait(NULL);

    kill(maintenance_managerPID, SIGINT);
    wait(NULL);

    kill(monitorPID, SIGINT);
    wait(NULL);

    kill(getpid(), SIGINT);
    wait(NULL);
    terminate();
    exit(0);
}

void sigtstp_handler() {
    write_log("SIGNAL SIGTSTP RECEIVED");
    print_stats();
}

// Creates new node for the queue
Node* newNode(int t_id, int ni, int t, int arr_time) {
    Node* temp = (Node*)malloc(sizeof(Node));
    temp->task_id = t_id;
    temp->nr_instructions = ni;
    temp->max_time = t;
    temp->arrive_time = arr_time;
    temp->next = NULL;
    return temp;
}

// Gets the top node of the queue
Node* peek(Node* head) { return head; }

// Removes the top node from the queue
void pop(Node** head) {
    Node* temp = *(head);
    (*head) = (*head)->next;
    // free(temp);
}

// Inserts a new node to the queue
void push(Node** head, Node* temp) {
    Node* start = (*head);
    if ((*head) == NULL) {
        (*head) = temp;
    } else if ((*head)->max_time > temp->max_time) {
        temp->next = *head;
        (*head) = temp;
    } else {
        while (start->next != NULL && start->next->max_time <= temp->max_time) {
            start = start->next;
        }
        temp->next = start->next;
        start->next = temp;
    }
}

bool isEmpty(Node** head) {
    if ((*head) == NULL) {
        return true;
    } else {
        return false;
    }
}