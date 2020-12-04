#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define SIZE 512
#define MSG_CODE 30
struct message {
    long mtype;
    int node_id;
    int request_id;
    char mtext[SIZE];
};

void send_to_print_server(int num_lines);


int main() {
    while (1) {
        send_to_print_server(1);
    }
}


void send_to_print_server(int num_lines) {
    key_t key = ftok("myqueue", 65);
    int qid = msgget(key, IPC_CREAT | 0666);

    for (int i = 1; i < num_lines + 1; i++) {
        struct message line;
        line.mtype = MSG_CODE;
        sprintf(line.mtext, "I am a hacker\n");
        msgsnd(qid, &line, SIZE, 0);
        sleep(2);
    }
}