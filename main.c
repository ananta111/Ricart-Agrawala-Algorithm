#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <memory.h>
#include <unistd.h>
#include <semaphore.h>
#include <time.h>
#include<signal.h>

#define SIZE 512
struct message {
    long mtype;
    int node_id;
    int request_id;
    char mtext[SIZE];
};

int main() {
    while (1) {
        struct message msg;
        key_t key = ftok("myqueue", 65);
        int qid = msgget(key, IPC_CREAT | 0666);
        msgrcv(qid, &msg, SIZE, 30, 0);
        printf("%s\n", msg.mtext);
        fflush(stdout);
    }
}