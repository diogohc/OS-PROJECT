// Diogo Miguel Henriques Correia (2016219825)
// Henrique José Gouveia Lobo (2020225959)

#include "declarations.h"

void system_manager() {
    pid_t mainPID;
    mainPID = getpid();

    signal(SIGTSTP, SIG_IGN);
    //signal(SIGINT, SIG_IGN);

    pthread_mutex_lock(&mutex_write);
    config->terminate_flag = false;
    pthread_mutex_unlock(&mutex_write);

    // Create message queue
    mqid = msgget(IPC_PRIVATE, IPC_CREAT | 0700);
    if (mqid == -1) {
        write_log("ERROR CREATING MESSAGE QUEUE");
    }

    // processo task manager
    if ((task_managerPID = fork()) == 0) {
        signal(SIGTSTP, SIG_IGN);
        //signal(SIGINT, SIG_IGN);
        task_manager();

    } else if (task_managerPID == -1) {
        write_log("ERROR CREATING TASK MANAGER PROCESS");
    }

    // processo monitor
    if ((monitorPID = fork()) == 0) {
        signal(SIGTSTP, SIG_IGN);
        //signal(SIGINT, SIG_IGN);
        monitor();

    } else if (monitorPID == -1) {
        write_log("ERROR CREATING MONITOR PROCESS");
    }

    // processo maintenance manager
    if ((maintenance_managerPID = fork()) == 0) {
        signal(SIGTSTP, SIG_IGN);
        //signal(SIGINT, SIG_IGN);
        maintenance_manager();

    } else if (maintenance_managerPID == -1) {
        write_log("ERROR CREATING MAINTENANCE MANAGER PROCESS");
    }

    if (getpid() == mainPID) {
        signal(SIGTSTP, sigtstp_handler);
        //signal(SIGINT, sigint_handler);
    }

    for (int i = 0; i < 3; i++) {
        wait(NULL);
    }
}

int main(int argc, char **argv) {
    initiate();

    if (argc != 2) {
        printf("Error! Use: ./offload_simulator <config_file>\n");
        return 1;
    }

    // ler ficheiro de configuracao
    if (read_file(argv[1]) == -1) {
        return 1;
    }

    system_manager();

    terminate();
    return 0;
}