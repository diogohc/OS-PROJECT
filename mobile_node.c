// Diogo Miguel Henriques Correia (2016219825)
// Henrique José Gouveia Lobo (2020225959)
#include "declarations.h"

char* build_task_command(int task_id, int n_instructions, int execution_time,
                         char* s) {
    char buffer[SIZE];
    memset(buffer, 0, SIZE);
    sprintf(buffer, "%d", task_id);
    strcat(s, buffer);
    strcat(s, ":");

    memset(buffer, 0, SIZE);
    sprintf(buffer, "%d", n_instructions);
    strcat(s, buffer);
    strcat(s, ":");

    memset(buffer, 0, SIZE);
    sprintf(buffer, "%d", execution_time);
    strcat(s, buffer);

    return s;
}

int main(int argc, char** argv) {
    int fd;
    int i = 0;
    char s[SIZE];

    if (argc != 5) {
        printf("Wrong number of arguments\n");
        printf("You should call the function like this:\n");
        printf(
            "mobile_node {nº pedidos a gerar} {intervalo entre pedidos em ms} "
            "{milhares de instruções de cada pedido} {tempo máximo para "
            "execução}\n");
        return -1;
    }

    if ((fd = open(PIPE_NAME, O_WRONLY)) < 0) {
        perror("Cannot open pipe for writing: ");
        exit(0);
    }

    int n_requests = atoi(argv[1]);
    int time_interval = atoi(argv[2]);
    int n_instructions = atoi(argv[3]);
    int execution_time = atoi(argv[4]);
    printf("%d, %d, %d, %d\n", n_requests, time_interval, n_instructions,
           execution_time);

    while (i < n_requests) {
        printf("ID:%d\n", i);
        s[0] = 0;
        // build_task_command(config->id_of_task, n_instructions,
        // execution_time, s);
        build_task_command(i, n_instructions, execution_time, s);

        write(fd, s, sizeof(char) * SIZE);

        sleep(time_interval / 1000);

        i++;
    }

    close(fd);

    return 0;
}