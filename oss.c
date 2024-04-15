/*
 *
 *Project Title: Project 4 - Multilevel Feedback Queues
 *Author: David Pham
 * 3/7/2024
 *
 */

#define _POSIX_C_SOURCE 199309L


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


#define SHMKEY1 2031535
#define SHMKEY2 2031536
#define SHMKEY3 2031537
#define BUFF_SZ sizeof (int)
#define MAXDIGITS 3
#define PERMS 0644
 

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

struct resource{
    pid_t pid; //which process currently has it_value
};

struct PCB{
    int occupied; //Either true or false
    pid_t pid; //process id of child
    int startSeconds; //time when it was forked
    int startNano; //time when it was forked
    int blocked;
    int eventBlockedUntilSec;
    int eventBlockedUntilNano;
};

int *sharedSeconds;
int *sharedNano;
int shmidSeconds;
int shmidNano;


struct PCB processTable[20];
struct resource resourceTable[10][20];

int msqid;

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

typedef struct msgbuffer {
    long mtype;
    int intData;
    int quanta;
} msgbuffer;

typedef struct{
    int proc;
    int simul;
    int timelimit;
    int interval;
    char logfile[20];
} options_t;


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
            printf("%d         %d         %d         %d         %d\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);
        }
        
    } 
}

void fprintProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20], FILE *fptr){
    lfprintf(fptr, "OSS PID %d SysClockS: %d SysClockNano: %d\n", PID, SysClockS, SysClockNano);
    lfprintf(fptr, "Process Table:\n");
    lfprintf(fptr, "Entry     Occupied  PID       StartS    Startn\n"); 
    for(int i = 0; i<20; i++){
        if((processTable[i].occupied) == 1){
            lfprintf(fptr, "%d         %d         %d         %d         %d\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);
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

    //Set up structs defaults
   
    for(int i = 0; i < 20; i++){
            processTable[i].occupied = 0;
            processTable[i].pid = 0;
            processTable[i].startSeconds = 0;
            processTable[i].startNano = 0;
            processTable[i].blocked = 0;
            processTable[i].eventBlockedUntilSec = 0;
            processTable[i].eventBlockedUntilNano = 0;
    }

    //Initiate resources
    for(int i = 0; i < 10; i++){
        for(int j = 0; j < 20; j++){
            resourceTable[i][j].pid = 0;
        }
    }

    //Set up user parameters
    options_t options;
    options.proc = 2; //n
    options.simul = 2; //s
    options.timelimit = 50000000; //t
    options.interval = 1; //i
    strcpy(options.logfile, "msgq.txt"); //f

    //Set up user input

    const char optstr[] = "hn:s:t:i:f:";

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
            case 't':
                options.timelimit = atoi(optarg);
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
    buff.quanta = 0;


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

    //Variables
    int childrenLaunched = 0; 
    int simulCount = 0;
    int childrenFinishedCount = 0;
    int currentChild = 0;
    int currentQueue = 0;
    int nextIntervalSeconds;
    int nextIntervalNano;
    int launchFlag = 0;
    int terminationPercent = 2;
    int blockPercent = 2;
    int timeSlice = 0;
    int readyCount = 0; //Counts how many processes are ready to go

    //Statistic Variable
    double idle = 0;
    double waitTime = 0;
    double blockTime = 0;

    while(childrenFinishedCount < options.proc){
        
           
        
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




