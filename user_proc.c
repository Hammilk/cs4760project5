#include<stdio.h>
#include<math.h>
#include<sys/shm.h>
#include<sys/ipc.h>
#include<unistd.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/msg.h>
#include<string.h>

#define SHMKEY 2031535
#define BUFF_SZ sifeof(int)
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
    int intData;
    int quanta;
} msgbuffer;

int main(int argc, char** argv){
    msgbuffer buff;
    buff.mtype = 1;
    int msqid = 0;
    key_t key;

    if((key = ftok("oss.c", 1)) == -1){
        perror("ftok");
        exit(1);
    }

    if((msqid = msgget(key, PERMS)) == -1){
        perror("msgget in child")
    }
}
