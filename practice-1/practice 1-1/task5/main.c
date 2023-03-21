// ? Loc here: header modification to adapt pthread_setaffinity_np
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <utmpx.h>

void *thread1(void* dummy){
    assert(sched_getcpu() == 0);
    return NULL;
}

void *thread2(void* dummy){
    assert(sched_getcpu() == 1);
    return NULL;
}
int main(){
    pthread_t pid[2];
    int i;
    // ? LoC: Bind core here
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    // set affinity mask for cpu core 1
    CPU_SET(1, &cpuset);
    // set pthread attribute for cpu affinity
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset); 

    for(i = 0; i < 2; ++i){
        // 1 Loc code here: create thread and save in pid[2]
        pthread_create(&pid[i], &attr, thread2, NULL);
    }

    for(i = 0; i < 2; ++i){
        // 1 Loc code here: join thread
        pthread_join(pid[i], NULL);
    }
    return 0;
}
