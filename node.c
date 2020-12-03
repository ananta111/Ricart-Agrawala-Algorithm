#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <memory.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/sem.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include<sys/shm.h>

#define SIZE 512
#define N 4
#define REQ_CODE 10
#define REP_CODE 20
#define MSG_CODE 30


struct message {
    long mtype;
    int node_id;
    int request_id;
    char mtext[SIZE];
};


int receive_request();
int send_request();
int receive_reply();
int send_request_to_queue(int i,  int request_number);
void send_to_print_server(int num_lines);
int send_reply_to_queue(int i);
struct shmseg* initialize_shared_memory(int proj_id);
void print_shared_mem_contents(struct shmseg* shared_variables);
void P (int semid);
void V (int semid);
void print_sem_val(int semid, char* type);
void destory_ipcs_utils(int proj_id);
key_t get_mutex_key(int node_num);
key_t get_wait_sem_key(int node_num);


int pid1;
int pid2;
int pid3;


// shared variables
struct shmseg {
    int request_number;
    int highest_request_number;
    int outstanding_reply;
    int request_CS;
    int reply_deferred[4];
    int mutex;
    int wait_sem;
};

// union struct for semaphore

// my node number
int me;

void handle_sigint(int sig) {
    destory_ipcs_utils(me);

    kill(pid1, SIGKILL);
    kill(pid2, SIGKILL);
    kill(pid3, SIGKILL);
}


int main(int argc, char** argv) {
    signal(SIGINT, handle_sigint);
    if (argc == 2) {
        me = atoi(argv[1]);
    } else {
        perror("Please supply the node number as an arg");
        fflush(stdout);
        exit(1);
    }


    struct shmseg* shared_variables = initialize_shared_memory(me);

    key_t mutex_key = get_mutex_key(me);
    key_t wait_sem_key = get_wait_sem_key(me);

    shared_variables->mutex = semget(mutex_key, 1, IPC_CREAT | 0660);
    shared_variables->wait_sem = semget(wait_sem_key, 1, IPC_CREAT | 0660);

    // initialize semaphores
    union semun u_mutex;
    union semun u_sem_wait;

    u_mutex.val = 1;
    u_sem_wait.val = 0;

    if(semctl(shared_variables->mutex, 0, SETVAL, u_mutex) < 0) {
        perror("SEM CTL failed");
        exit(1);
    }

    if(semctl(shared_variables->wait_sem, 0, SETVAL, u_sem_wait) < 0) {
        perror("SEM CTL failed");
        exit(1);
    }

    if (shared_variables->mutex == -1) {
        perror("Error creating mutex semaphore");
    }

    if (shared_variables->wait_sem == -1) {
        perror("Error creating wait semaphore");
    }

    // initialise shared variables
    shared_variables->request_number = 0;
    shared_variables->highest_request_number = 0;
    shared_variables->outstanding_reply = 0;
    shared_variables->request_CS = 0;


    send_request();
    receive_request();
    receive_reply();

    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
    waitpid(pid3, NULL, 0);

}

int receive_request() {
    int pid = fork();
    pid1 = pid;
    if (pid < 0) {
        perror("Fork error");
        exit(1);
    }
    else if (pid == 0) {
        struct shmseg* shared_variables = initialize_shared_memory(me);
        key_t mutex_key = get_mutex_key(me);
        int mutex = semget(mutex_key, 1, IPC_CREAT | 0660);


        signal(SIGINT, handle_sigint);
        while (1) {
            int defer_it = 0;

            key_t key = ftok("myqueue", 65);
            int qid = msgget(key, IPC_CREAT | 0666);
            struct message msg_rcvd;

            msgrcv(qid, &msg_rcvd, SIZE, me + REQ_CODE, 0);
            printf("Received request from %d\n", msg_rcvd.node_id);
            fflush(stdout);

            // node id and request id
            int k = msg_rcvd.request_id;
            int i = msg_rcvd.node_id;

            if (k > shared_variables->highest_request_number) {
                shared_variables->highest_request_number = k;
            }

            P(mutex); // call sem get from every process

            if (shared_variables->request_CS == 1 && (k > shared_variables->request_number || (k == shared_variables->request_number && i > me) )) {
                defer_it = 1;
            }

            V(mutex);

            if (defer_it == 1) {
                shared_variables->reply_deferred[i] = 1;
                printf("Deferred reply to %d\n", i);
                fflush(stdout);
            } else {
                send_reply_to_queue(i);
                printf("Sent reply to %d\n", i);
                fflush(stdout);
            }
        }

    }

    return 1;
}

int receive_reply() {
    int pid = fork();

    pid2 = pid;
    if (pid < 0) {
        perror("Fork failed");
        exit(1);
    }

    else if (pid == 0) {
        struct shmseg* shared_variables = initialize_shared_memory(me);
        key_t wait_sem_key = get_wait_sem_key(me);

        int wait_sem = semget(wait_sem_key, 1, IPC_CREAT | 0660);

        signal(SIGINT, handle_sigint);
        while (1) {

            key_t key = ftok("myqueue", 65);
            int qid = msgget(key, IPC_CREAT | 0666);
            struct message msg;

            msgrcv(qid, &msg, SIZE, me + REP_CODE, 0);
            printf("Received reply from %d\n", msg.node_id);
            shared_variables->outstanding_reply = shared_variables->outstanding_reply - 1;
            V(wait_sem);
        }

    }

    return 1;

}

int send_request() {
    int pid = fork();

    pid3 = pid;
    if (pid < 0) {
        perror("Fork failed");
        exit(1);
    }

    else if (pid == 0) {
        struct shmseg* shared_variables = initialize_shared_memory(me);

        key_t mutex_key = get_mutex_key(me);
        key_t wait_sem_key = get_wait_sem_key(me);

        int mutex = semget(mutex_key, 1, IPC_CREAT | 0660);
        int wait_sem = semget(wait_sem_key, 1, IPC_CREAT | 0660);

        signal(SIGINT, handle_sigint);
        while (1) {
            P(mutex);

            shared_variables->request_CS = 1;
            shared_variables->request_number = shared_variables->highest_request_number + 1;

            V(mutex);

            shared_variables->outstanding_reply = N - 1;

            printf("I am at send request\n");
            fflush(stdout);
            for (int i = 0; i < N; i++) {
                if (i != me) {
                    send_request_to_queue(i, shared_variables->request_number);
                    printf("sent request to %d\n", i);
                    fflush(stdout);
                }
            }
            printf("I am done sending requests\n");

            printf("Outstanding Reply: %d\n", shared_variables->outstanding_reply);
            fflush(stdout);

            while (shared_variables->outstanding_reply != 0) {
                printf("Outstanding Reply Inside While Loop: %d\n", shared_variables->outstanding_reply);
                fflush(stdout);
                P(wait_sem);
            }

            printf("Outstanding Reply Before Critical Section: %d\n", shared_variables->outstanding_reply);
            fflush(stdout);

            //critical section
            printf("Reached critical section\n");
            send_to_print_server(3);
            printf("Completed the critical section\n");
            fflush(stdout);

            printf("Outstanding Reply After Critical Section: %d\n", shared_variables->outstanding_reply);
            fflush(stdout);

            shared_variables->request_CS = 0;

            for (int i = 0 ; i < N; i++) {
                if (shared_variables->reply_deferred[i] == 1) {
                    shared_variables->reply_deferred[i] = 0;
                    send_reply_to_queue(i);
                    printf("Previously deferred but replied now to %d\n", i);
                }
            }
            sleep(1);
        }
    }

    return 1;
}

int send_request_to_queue(int i, int request_number) {
    struct message msg;
    msg.node_id = me;
    msg.request_id = request_number;
    msg.mtype = i + REQ_CODE;

    key_t key = ftok("myqueue", 65);
    int qid = msgget(key, IPC_CREAT | 0666);
    msgsnd(qid, &msg, SIZE, 0);

    return 2;
}


int send_reply_to_queue(int i) {
    struct message msg;
    msg.node_id = me;
    msg.request_id = (int)time(NULL);
    msg.mtype = i + REP_CODE;

    key_t key = ftok("myqueue", 65);
    int qid = msgget(key, IPC_CREAT | 0666);
    msgsnd(qid, &msg, SIZE, 0);

    return 2;
}


void send_to_print_server(int num_lines) {
    key_t key = ftok("myqueue", 65);
    int qid = msgget(key, IPC_CREAT | 0666);

    struct message start_msg;
    start_msg.mtype = MSG_CODE;
    sprintf(start_msg.mtext, "###### START OUTPUT FOR NODE %d ######", me);
    msgsnd(qid, &start_msg, SIZE, 0);

    for (int i = 1; i < num_lines + 1; i++) {
        struct message line;
        line.mtype = MSG_CODE;
        sprintf(line.mtext, "%d This is line %d", me, i);
        msgsnd(qid, &line, SIZE, 0);
    }

    struct message end_msg;
    end_msg.mtype = MSG_CODE;
    sprintf(end_msg.mtext, "------ END OUTPUT FOR NODE %d ------", me);
    msgsnd(qid, &end_msg, SIZE, 0);

}

struct shmseg* initialize_shared_memory(int proj_id) {
    int shmid;
    struct shmseg *shmp;

    key_t shmkey = ftok("shmfile", proj_id);

    shmid = shmget(shmkey, sizeof(struct shmseg), 0644| IPC_CREAT);
    if (shmid == -1) {
        perror("Error Creating Shared memory");
        exit(1);
    }
    // Attach to the segment to get a pointer to it.
    shmp = shmat(shmid, NULL, 0);
    if (shmp == (void *) -1) {
        perror("Attach shared memory");
        exit(1);
    }

    return shmp;
}


void P (int semid) {
    struct sembuf p = { 0, -1, 0};
    if(semop(semid, &p, 1) < 0) {
        perror("P operating failed");
        exit(1);
    }
}

void V (int semid) {
    struct sembuf v = { 0, 1, 0};
    if(semop(semid, &v, 1) < 0) {
        perror("V operating failed");
        exit(1);
    }
}

void print_sem_val(int semid, char* type) {

    if(semctl(semid, 0, GETVAL) < 0) {
        perror("SEM CTL failed");
        exit(1);
    }

    printf("%s Semaphore Value: %d ", type, semctl(semid, 0,  GETVAL));
    fflush(stdout);
}


void print_shared_mem_contents(struct shmseg* shared_variables) {
    printf("req num: %d\n", shared_variables->request_number);
    printf("highest req num: %d\n", shared_variables->highest_request_number);
    printf("outstanding replies: %d\n", shared_variables->outstanding_reply);
    printf("request CS: %d ", shared_variables->request_CS);
    fflush(stdout);
}


void destory_ipcs_utils(int proj_id) {
    int shmid;
    struct shmseg *shmp;

    key_t shmkey = ftok("shmfile", proj_id);

    shmid = shmget(shmkey, sizeof(struct shmseg), 0644| IPC_CREAT);

    if (shmid == -1) {
        perror("Error Creating Shared memory");
        fflush(stdout);
        exit(1);
    }

    // Attach to the segment to get a pointer to it.
    shmp = shmat(shmid, NULL, 0);

    if (shmp == (void *) -1) {
        perror("Error attaching shared memory in destroy ipcs utils method\n");
    }

    // delete shared memory
    shmdt(shmp);
    shmctl(shmid, IPC_RMID, NULL);


    key_t mutex_key = get_mutex_key(proj_id);
    key_t wait_sem_key = get_wait_sem_key(proj_id);

    printf("Mutex Key: %d Wait Key: %d\n", mutex_key, wait_sem_key);
    fflush(stdout);


    int mutex = semget(mutex_key, 1, IPC_CREAT | 0660);
    int wait_sem = semget(wait_sem_key, 1, IPC_CREAT | 0660);

    printf("Mutex: %d Wait: %d\n", mutex, wait_sem);
    fflush(stdout);

    // delete semaphores
    if (semctl(mutex, 1, IPC_RMID) == -1) {
        perror ("Error deleting mutex semaphore");
        fflush(stdout);
    }

    if (semctl(wait_sem, 1, IPC_RMID) == -1) {
        perror ("Error Deleting wait semaphore");
        fflush(stdout);
    }

    //destroy message queue
    key_t key = ftok("myqueue", 65);
    int qid = msgget(key, IPC_CREAT | 0666);

    if (msgctl(qid, IPC_RMID, NULL) < 0) {
        perror("Error deleting message queue");
        fflush(stdout);
    };

}


key_t get_mutex_key(int node_num) {
    key_t key = getuid() + node_num;
    return key;
}

key_t get_wait_sem_key(int node_num) {
    key_t key = getuid() + node_num + 5;
    return key;
}







