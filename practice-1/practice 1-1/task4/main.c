// ? Loc here: header modification to adapt pthread_cond_t
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#define MAXTHREAD 10
// declare cond_variable: you may define MAXTHREAD variables
pthread_cond_t cond;
pthread_mutex_t lock; 
// flag that indicates whether Thr#9 has left its critical section
int flag = 0;

// ? Loc in thread1: you can do any modification here, but it should be less than 20 Locs
// the following code ensures that Thr#9 executes before Thr#0 ~ Thr#8,
// while Thr#0 ~ Thr#8 are executed in a random order
void *thread1(void* dummy) {
    int i;
    int thr = *((int*)dummy);
    pthread_mutex_lock(&lock);
    if (thr != MAXTHREAD - 1) {
        // need and only need to sleep if Thr#9 hasnt left its critical section 
        // in this case WHILE can be replaced by IF
        while (!flag) {
            pthread_cond_wait(&cond, &lock);
        }
    }
    printf("This is thread %d!\n", thr);
    for(i = 0; i < 20; ++i){
        printf("H");
        printf("e");
        printf("l");
        printf("l");
        printf("o");
        printf("W");
        printf("o");
        printf("r");
        printf("l");
        printf("d");
        printf("!");
    }
    if (thr == MAXTHREAD - 1) {
        // Thr#9 is leaving its critical section
        // set flag to 1 before lock is released 
        flag = 1;
    }
    pthread_mutex_unlock(&lock);
    if (thr == MAXTHREAD - 1) {
        // awake all the sleeping threads 
        pthread_cond_broadcast(&cond);
    }
    return NULL;
}

int main(){
    pthread_t pid[MAXTHREAD];
    int i;
    // ? Locs: initialize the cond_variables
    pthread_cond_init(&cond, NULL);
    pthread_mutex_init(&lock, NULL);
    for(i = 0; i < MAXTHREAD; ++i){
        int* thr = (int*) malloc(sizeof(int)); 
        *thr = i;
        // 1 Loc here: create thread and pass thr as parameter
        pthread_create(&pid[i], NULL, thread1, thr);
    }
    for(i = 0; i < MAXTHREAD; ++i)
        // 1 Loc here: join thread
        pthread_join(pid[i], NULL);
    return 0;
}
