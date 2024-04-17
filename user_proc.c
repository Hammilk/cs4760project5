#include<stdio.h>
#include<math.h>
#include<sys/shm.h>
#include<sys/ipc.h>
#include<unistd.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/msg.h>

//Various Macros
#define SHMKEY1 2031535
#define SHMKEY2 2031536
#define BUFF_SZ sizeof(int)
#define PERMS 0644
#define reqChance 80 //defines the chance that the process makes a request
#define termChance 2 //defines the chance that a process terminates within a loop

//Randomizer Section

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
    if(!randomize_helper(fopen("/dev/urandom", "r")))
        return 0;
    if(!randomize_helper(fopen("/dev/arandom", "r")))
        return 0;
    if(!randomize_helper(fopen("/dev/random", "r")))
        return 0;
    return -1;
}

//Set up struct for message queue
typedef struct{
    long mtype;
    int resource; //+ for request, - for giving
} msgbuffer;

int main(int argc, char** argv){ //at some point, add bound parameter
    //Arg[1] bound

    //Set up message queue stuff
    msgbuffer buff;
    buff.mtype = 1;
    int msqid = 0;
    key_t key;

    if((key = ftok("oss.c", 1)) == -1){
        perror("ftok");
        exit(1);
    }
    if((msqid = msgget(key, PERMS)) == -1){
        perror("msgget in child");
        exit(1); 
    }

    //Set up shared memory pointers
    int shm_id = shmget(SHMKEY1, BUFF_SZ, IPC_CREAT | 0666);
    if(shm_id <= 0){
        fprintf(stderr, "Shared memory get failed.\n");
        exit(1);
    }
    int* sharedSeconds = shmat(shm_id, 0, 0);

    shm_id = shmget(SHMKEY2, BUFF_SZ, IPC_CREAT | 0666);
    if(shm_id <= 0){
        fprintf(stderr, "Shared memory get failed.\n");
        exit(1);
    }
    int* sharedNano = shmat(shm_id, 0, 0);

    //Parse out current SysClock and SysNano time
    int sysClockS = *sharedSeconds; //Starting seconds
    int sysClockNano = *sharedNano; //Starting nanoseconds
    
    //Seed random 
    if(randomize()){
        fprintf(stderr, "Warning: No sources for randomness.\n");
    }

    //Initialize bound
    int bound = rand() % (atoi(argv[1]) + 1);
    
    //Initalize request time
    int requestNano = sysClockNano + bound;
    int requestSecond = sysClockS;
    if(requestNano > pow(10, 9)){
        requestNano -= (pow(10, 9));
        requestSecond++;
    }

    //Set up array to track resources
    int resourceArray[10];
    for(int i = 0; i < 10; i++){
        resourceArray[i] = 0;
    }
    //Set up control for work loop
    int termFlag = 0;

    //Work section
    while(!termFlag){
        //check if time to request
        if(requestSecond > *sharedSeconds || (requestSecond == *sharedSeconds) && (requestNano > *sharedNano)){
            //Determine whether to request or release 
            int requestGenerate = rand() % 101;
            if(requestGenerate > reqChance){ //if requestGenerate is higher than reqchance, request
                buff.resource = rand() % 10;
                while(resourceArray[buff.resource] > 20){
                    buff.resource = rand() % 10;
                }
            }
            else{ //Release section
                buff.resource = -(rand() % 10);
                while(resourceArray[-buff.resource] < 0){ //Checks to make sure child doesn't request more processes then what exits
                    buff.resource = -(rand() % 10);
                }
            }
            //Send request/release
            if(msgsnd(msqid, &buff, sizeof(msgbuffer) - sizeof(long), 0) == -1){
                perror("msgsnd to parent failed\n");
                exit(1);
            }
            if(buff.resource > 0){
                printf("Requesting for resource %d\n", buff.resource);
            }
            else{
                printf("Releasing resource %d\n", -buff.resource);
            }
            //If sent request message, blocking recieved until child receives resource
            if(buff.resource > -1){
                if(msgrcv(msqid, &buff, sizeof(msgbuffer), getpid(), 0)){
                    perror("msgsnd to parent failed");
                    exit(1);
                }
                //Increment resource in resource array to track
                resourceArray[buff.resource]++;
                //Reset bounds 
                bound = rand() % (atoi(argv[1]) + 1);
                requestNano = sysClockNano + bound;
                requestSecond = requestSecond;
                if(requestNano > pow(10, 9)){
                    requestNano -= (pow(10, 9));
                    requestSecond++;
                }
            }
            else{
                //Decrement to indicate releasing resource
                resourceArray[buff.resource]--;
            }
        }     

        //Determinte if process should terminate
        int terminateGenerate = rand() % 201;
        if(terminateGenerate < termChance){
            termFlag = 1;
        }
    }
}
