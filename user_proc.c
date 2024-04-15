#include<stdio.h>
#include<math.h>
#include<sys/shm.h>
#include<sys/ipc.h>
#include<unistd.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/msg.h>
#include<string.h>

#define SHMKEY1 2031535
#define SHMKEY2 2031536
#define BUFF_SZ sizeof(int)
#define PERMS 0644

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

typedef struct msgbuffer{
    long mtype;
    int resourceRequest; //+ for request, - for giving
} msgbuffer;

static int randomize(void){
    if(!randomize_helper(fopen("/dev/urandom", "r")))
        return 0;
    if(!randomize_helper(fopen("/dev/arandom", "r")))
        return 0;
    if(!randomize_helper(fopen("/dev/random", "r")))
        return 0;
    return -1;
}


int main(int argc, char** argv){ //at some point, add bound parameter
    //Arg[1] bound

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
    //Check
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

    //Upper bound
    int bound = atoi(argv[1]);

    //Work section
    while(TODO){
        //check if time to request
        //if so, calculate request vs release
        //then send message to request


        if(msgrcv(msqid, &buff, sizeof(msgbuffer), getpid(), 0) == -1){
            perror("Failed to receive message\n");
            exit(1);
        }
        //Calculate if terminate


        
    }
}
