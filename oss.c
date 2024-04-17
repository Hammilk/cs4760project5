/*
 *Project Title: Project 5 - Resource Management
 *Author: David Pham
 * 3/7/2024
 */

#include<time.h>
#include<stdio.h>
#include<sys/types.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<math.h>
#include<signal.h>
#include<sys/time.h>
#include<getopt.h>
#include<string.h>
#include<sys/msg.h>
#include<stdarg.h>
#include<errno.h>

//Macros
#define SHMKEY1 2031535
#define SHMKEY2 2031536
#define BUFF_SZ sizeof (int)
#define MAXDIGITS 3
#define PERMS 0644
#define BOUND 20

//Globals
struct PCB{
    int occupied; //Either true or false
    pid_t pid; //process id of child
    int startSeconds; //time when it was forked
    int startNano; //time when it was forked
    int blocked;
};

//Message struct
typedef struct {
    long mtype;
    int resource;
} msgbuffer;

//Optarg struct
typedef struct{
    int proc;
    int simul;
    int interval;
    char logfile[20];
} options_t;

//Shared Memory pointers
int *sharedSeconds;
int *sharedNano;
int shmidSeconds;
int shmidNano;

//Process Table
struct PCB processTable[20];

//Message ID
int msqid;

struct QNode{
    int key;
    struct QNode* next;
};

struct Queue{
    struct QNode *front, *rear;
};

//Function prototypes
int deadlock(const int *available, const int m, const int n, const int *request, const int *allocated);
int req_lt_avail(const int *req, const int *avail, const int pnum, const int num_res);
void printResourceTable(int resourceTable[20][10]);
int addProcessTable(struct PCB processTable[20], pid_t pid);
struct QNode* newNode(int k);
struct Queue* createQueue();
void enQueue(struct Queue* q, int k);
void deQueue(struct Queue* q);
int lfprintf(FILE *stream,const char *format, ... );
static int setupinterrupt(void);
static int setupitimer(void);
void print_usage(const char * app);
void printProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20]);
void fprintProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20], FILE *fptr);
void incrementClock(int *seconds, int *nano, int increment);
static int randomize_helper(FILE *in);
static int randomize(void);
void clearResources();
void resourceControl(int resourceTable[20][10], int *availableResources, pid_t pid, int resource, msgbuffer buff);
int clearProcessTable(struct PCB processTable[20], pid_t pid);

int main(int argc, char* argv[]){

    //Seed random
    if(randomize()){
        fprintf(stderr, "Warning: No source for randomness.\n");
    }

    //Set up shared memory
    shmidSeconds = shmget(SHMKEY1, BUFF_SZ, 0666 | IPC_CREAT);
    if(shmidSeconds == -1){
        fprintf(stderr, "error in shmget 1.0\n");
        exit(1);
    }
    sharedSeconds = shmat(shmidSeconds, 0, 0);
    
    //Attach shared memory to nano
    shmidNano = shmget(SHMKEY2, BUFF_SZ, 0777 | IPC_CREAT);
    if(shmidNano == -1){
        fprintf(stderr, "error in shmget 2.0\n");
        exit(1);
    }
    sharedNano=shmat(shmidNano, 0, 0);

    //Set up resources
    int resourceTable[20][10];
    for(int i = 0; i < 20; i++){
        for(int j = 0; j < 10; j++){
            resourceTable[i][j] = 0;
        }
    }
    int availableResources[10];
    for(int i = 0; i < 10; i++){
        availableResources[i] = 20;
    }

    //Set up structs defaults
    for(int i = 0; i < 20; i++){
        processTable[i].occupied = 0;
        processTable[i].pid = 0;
        processTable[i].startSeconds = 0;
        processTable[i].startNano = 0;
    }

    //Set up user parameters
    options_t options;
    options.proc = 2; //n
    options.simul = 2; //s
    options.interval = 1; //i
    strcpy(options.logfile, "msgq.txt"); //f

    //Set up user input
    const char optstr[] = "hn:s:i:f:";
    char opt;
    while((opt = getopt(argc, argv, optstr))!= -1){
        switch(opt){
            case 'h':
                print_usage(argv[0]);
                return(EXIT_SUCCESS);
            case 'n':
                options.proc = atoi(optarg);
                break;
            case 's':
                options.simul = atoi(optarg);
                break;
            case 'i':
                options.interval = atoi(optarg);
                break;
            case 'f':
                strcpy(options.logfile, optarg);
                break;
            default:
                printf("Invalid options %c\n", optopt);
                print_usage(argv[0]);
                return(EXIT_FAILURE);
        }
    }
   
    //Set up variables;
    pid_t pid;
    int seconds = 0;
    int nano = 0;
    *sharedSeconds = seconds;
    *sharedNano = nano;

    //Variables for message queue
    key_t key;
    msgbuffer buff;
    buff.mtype = 1;
    buff.resource = 0;

    //Set up timers
    if(setupinterrupt() == -1){
        perror("Failed to set up handler for SIGPROF");
        return 1;
    }
    if(setupitimer() == -1){
        perror("Failed to set up the ITIMER_PROF interval timer");
        return 1;
    }

    //Set up file
    char commandString[20];
    strcpy(commandString, "touch "); 
    strcat(commandString, options.logfile);
    system(commandString);
    FILE *fptr;
    fptr = fopen(options.logfile, "w");
    if(fptr == NULL){
        fprintf(stderr, "Error: file has not opened.\n");
        exit(0);
    }

    //get a key for message queue
    if((key = ftok("oss.c", 1)) == -1){
        perror("ftok");
        exit(1);
    }

    //create our message queue
    if((msqid = msgget(key, PERMS | IPC_CREAT)) == -1){
        perror("msgget in parent");
        exit(1);
    }
    
    struct Queue *blockedQueue[10];
    for(int i = 0; i < 10; i++){
        blockedQueue[i] = createQueue();
    }

    //Variables
    int simulCount = 0;
    int childrenFinishedCount;
    int nextIntervalSecond = 0;
    int nextIntervalNano = 0;
    int childLaunchedCount = 0;
    pid_t terminatedChild = 0;
    int status; //Where status of terminated pid will be stored
    int blockedCount = 0;
    int altFlag = 0;

    while(childrenFinishedCount < options.proc){
        pid_t child = 0;
        //nonblocking pid
        if(!(terminatedChild = waitpid(0, &status, WNOHANG))){
            if(!WEXITSTATUS(status)){
                perror("Child Aborted Abnormally");
                exit(1);
            }
            //if terminated child clear resources and process table entry
            resourceControl(resourceTable, availableResources, terminatedChild, -100, buff);
            if(clearProcessTable(processTable, terminatedChild) == -1){
                perror("Process Clear Aborted Abnormally");
                exit(1);
            }
            simulCount--;
            childrenFinishedCount++;
        }
        //launch child
        if(childLaunchedCount < options.proc && 
            simulCount < options.simul && 
            (*sharedSeconds > nextIntervalSecond || *sharedSeconds == nextIntervalNano && *sharedNano > nextIntervalSecond) &&
            (child = fork()) > 0){
                        
            //Child Launch Section
            char passedBound[MAXDIGITS];
            sprintf(passedBound, "%d", BOUND);

            char * args[] = {"./user_proc.c", passedBound};
            
            //Run Executable
            execlp(args[0], args[0], args[1], NULL);
            printf("Exec failed\n");
            exit(1);
        }
        if(child > 0){
            if(addProcessTable(processTable, child) == -1){
                perror("Add process table failed");
                exit(1);
            }
            simulCount++;
            childLaunchedCount++;
        }
            
        //grant outstanding requests
        if(blockedCount > 0){
            for(int i = 0; i < 10; i++){
                while(availableResources[i] > 0 && (blockedQueue[i]->front) != NULL){
                    resourceControl(resourceTable, availableResources, processTable[blockedQueue[i]->front->key].pid, i, buff);
                    blockedCount--;
                }
            }
        }

        //check messages and grant/release resource
        for(int i = 0; i < 20; i++){
            if(msgrcv(msqid, &buff, sizeof(buff) - sizeof(long), 0, IPC_NOWAIT)==-1){
                if(errno == ENOMSG){
                    printf("Got no message so maybe do nothing?\n");
                }
                else{
                    printf("Got an error from msgrcv\n");
                    perror("msgrcv");
                    exit(1);
                }
            }
            else{
                resourceControl(resourceTable, availableResources, processTable[i].pid, buff.resource, buff); //Grant or release resource    
            }
        }
        //every half a second, output resource table and process table to logfile and screen
        if(altFlag == 0 && (*sharedNano) > (pow(10, 9) / 2)){
            altFlag = 1;
            printProcessTable(getpid(), *sharedSeconds, *sharedNano, processTable);
            fprintProcessTable(getpid(), *sharedSeconds, *sharedNano, processTable, fptr);
            printResourceTable(resourceTable);
        }
        if(altFlag == 1 && (*sharedNano) < (pow(10, 9) / 2)){
            altFlag = 0;
            printProcessTable(getpid(), *sharedSeconds, *sharedNano, processTable);
            fprintProcessTable(getpid(), *sharedSeconds, *sharedNano, processTable, fptr);
            printResourceTable(resourceTable);
        }

        //deadlock detection algorithm
        
        //if deadlock, terminate some processes until deadlock is gone
        
    }
    //Remove message queues 
    if(msgctl(msqid, IPC_RMID, NULL) == -1){
        perror("msgctl to get rid of queue in parent failed");
        exit(1);
    }
    
    //Remove shared memory
    shmdt(sharedSeconds);
    shmdt(sharedNano);
    shmctl(shmidSeconds, IPC_RMID, NULL);
    shmctl(shmidNano, IPC_RMID, NULL);
    

    //Close file
    fclose(fptr);
    return 0;
    
}
// print no more than 10k lines to a file
int lfprintf(FILE *stream,const char *format, ... ) {
    static int lineCount = 0;
    lineCount++;
    if (lineCount > 10000)
        return 1;
    va_list args;
    va_start(args, format);
    vfprintf(stream,format, args);
    va_end(args);
    return 0;
}

static void myhandler(int s){
    printf("Got signal, terminated\n");
    for(int i = 0; i < 20; i++){
        if(processTable[i].occupied == 1){
            kill(processTable[i].pid, SIGTERM);
        }
    }
    if(msgctl(msqid, IPC_RMID, NULL) == -1){
        perror("msgctl to get rid of queue in parent failed");
        exit(1);
    }
    shmdt(sharedSeconds);
    shmdt(sharedNano);
    shmctl(shmidSeconds, IPC_RMID, NULL); 
    shmctl(shmidNano, IPC_RMID, NULL);
    exit(1);
}

static int setupinterrupt(void){
    struct sigaction act;
    act.sa_handler = myhandler;
    act.sa_flags = 0;
    return(sigemptyset(&act.sa_mask) || sigaction(SIGINT, &act, NULL) || sigaction(SIGPROF, &act, NULL));
}

static int setupitimer(void){
    struct itimerval value;
    value.it_interval.tv_sec = 5;
    value.it_interval.tv_usec = 0;
    value.it_value = value.it_interval;
    return (setitimer(ITIMER_PROF, &value, NULL));
}

void print_usage(const char * app){
    fprintf(stderr, "usage: %s [-h] [-n proc] [-s simul] [-t timeLimitForChildren] [-i intervalInMsToLaunchChildren] [-f logfile]\n", app);
    fprintf(stderr, "   proc is the total amount of children.\n");
    fprintf(stderr, "   simul is how many children can run simultaneously.\n");
    fprintf(stderr, "   timeLimitForChildren is the bound of time that a child process should be launched for.\n");
    fprintf(stderr, "   intervalInMsToLaunchChildren specifies how often you should launch a child.\n");
    fprintf(stderr, "   logfile is the input for the name of the logfile for oss to write into.\n");
}

void printProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20]){
    printf("OSS PID %d SysClockS: %d SysClockNano: %d\n", PID, SysClockS, SysClockNano);
    printf("Process Table:\n");
    printf("Entry     Occupied  PID       StartS    Startn\n"); 
    for(int i = 0; i<20; i++){
        if((processTable[i].occupied) == 1){
            printf("%d         %d         %d         %d         %d\n", i, processTable[i].occupied,
                   processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);
        }
    } 
}

void fprintProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20], FILE *fptr){
    lfprintf(fptr, "OSS PID %d SysClockS: %d SysClockNano: %d\n", PID, SysClockS, SysClockNano);
    lfprintf(fptr, "Process Table:\n");
    lfprintf(fptr, "Entry     Occupied  PID       StartS    Startn\n"); 
    for(int i = 0; i<20; i++){
        if((processTable[i].occupied) == 1){
            lfprintf(fptr, "%d         %d         %d         %d         %d\n", i, processTable[i].occupied, 
                     processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);
        }
    } 
}

void printResourceTable(int resourceTable[20][10]){
    printf("  ");
    for(int i = 0; i < 10; i++){
        printf("R%d  ", i);
    }
    printf("\n");
    for(int i = 0; i < 20; i++){
        printf("P%d  ", i);
        for(int j = 0; j < 10; j++){
            printf("%d  ", resourceTable[i][j]);
        }
    }
}

void incrementClock(int *seconds, int *nano, int increment){
    (*nano) += increment;
    if((*nano) >= (pow(10, 9))){
         (*nano) -= (pow(10, 9));
         (*seconds)++;
    }
}

static int randomize_helper(FILE *in){
    unsigned int seed;
    if(!in) return -1;
    if(fread(&seed, sizeof seed, 1, in) == 1){
        fclose(in);
        srand(seed);
        return 0;
    }
    fclose(in);
    return -1;
}

static int randomize(void){
    if(!randomize_helper(fopen("/dev/urandom", "r"))) return 0;
    if(!randomize_helper(fopen("/dev/arandom", "r"))) return 0;
    if(!randomize_helper(fopen("/dev/random", "r"))) return 0;
    return -1;
}

void resourceControl(int resourceTable[20][10], int *availableResources, pid_t pid, int resource, msgbuffer buff){
    int index = 0;
    for(int i = 0; i < 20; i++){
        if(processTable[i].pid == pid){
            index = i;
        }
    }
    if(resource == -100){ //When process terminates and releases all resources
        for(int i = 0; i < 10; i++){
            availableResources[i] += resourceTable[index][i];
            resourceTable[index][i] = 0;
        }
    }
    if(resource < 0){ //Release resource to process
        availableResources[-resource]++;
        resourceTable[index][-resource]--;
    }
    else if(resource > 0){ //grant resource to process
        availableResources[resource]--;
        resourceTable[index][resource]++;
        buff.mtype = pid;
        buff.resource = resource;
        if(msgsnd(msqid, &buff, sizeof(msgbuffer) - sizeof(long), 0)){
            perror("Msgsend to child failed");
            exit(1);
        }
    }
    else{
        perror("Resource error");
        exit(1);
    }
}

int clearProcessTable(struct PCB processTable[20], pid_t pid){
    for(int i = 0; i < 20; i++){
        if(processTable[i].pid == pid){
            processTable[i].occupied = 0;
            processTable[i].pid = 0;
            processTable[i].startSeconds = 0;
            processTable[i].startNano = 0;
            processTable[i].blocked = 0;
            return 0; 
        }
    }
    return -1;
}

int addProcessTable(struct PCB processTable[20], pid_t pid){
    for(int i = 0; i < 20; i++){
        if(!processTable[i].occupied){
            processTable[i].occupied = 1;
            processTable[i].pid = pid;
            processTable[i].startNano = *sharedNano;
            processTable[i].startSeconds = *sharedSeconds;
            return 0;
        }
    }
    return -1;
}

//Create new node function
struct QNode* newNode(int k){
    struct QNode* temp = (struct QNode*)malloc(sizeof(struct QNode));
    temp->key = k;
    temp->next = NULL;
    return temp;
}

//Create Queue function
struct Queue* createQueue(){
    struct Queue* q = (struct Queue*)malloc(sizeof(struct Queue));
    q->front = q->rear = NULL;
    return q;
}

//The function to add a key k to q
void enQueue(struct Queue* q, int k){
    struct QNode* temp = newNode(k);

    if(q->rear == NULL){
        q->front = q->rear = temp;
        return;
    }

    q->rear->next = temp;
    q->rear = temp;
}

//Function to remove a key from given queue q
void deQueue(struct Queue* q)
{
    if(q->front == NULL){
        return;
    }

    struct QNode* temp = q->front;

    q->front = q->front->next;

    if(q->front == NULL){
        q->rear = NULL;
    }
    free(temp);
}

int deadlock(const int *available, const int m, const int n, const int *request, const int *allocated){
    int work[m];
    int finish[n];

    for(int i = 0; i < m; i++){
        work[i] = available[i];
    }
    for(int i = 0; i < n; i++){
        finish[i] = 0;
    }

    int p = 0;
    for(; p < n; p++){
        if(finish[p]){
            continue;
        }
        if(req_lt_avail(request, work, p, m)){
            finish[p] = 1;
            for(int i = 0; i < m; i++){
                work[i] += allocated[p*m+i];
            }
            p = -1;
        }
    }
    for(p = 0; p < n; p++){
        if(!finish[p]){
            break;
        }
    }
    return (p != n);
}

int req_lt_avail(const int *req, const int *avail, const int pnum, const int num_res){
    int i = 0;
    for(; i < num_res; i++){
        if(req[pnum*num_res+i] > avail[i]){
            break;
        }
    }
    return (i == num_res);
}




















