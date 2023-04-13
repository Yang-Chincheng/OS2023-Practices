#include "coroutine.h"

typedef pthread_rwlock_t co_lock_t;
#define RDLOCK(lock) pthread_rwlock_rdlock(lock)
#define WRLOCK(lock) pthread_rwlock_wrlock(lock)
#define UNLOCK(lock) pthread_rwlock_unlock(lock)
#define INITLOCK(lock, attr) pthread_rwlock_init(lock, attr)

typedef void co_arg_t;
typedef int co_ret_t;
typedef co_ret_t (*co_func_t)(co_arg_t);
typedef int co_status_t;

/* Implementation of Array List */

typedef void* co_array_value_t;

typedef struct co_array_t {
    size_t len;
    size_t cap;
    co_array_value_t *data;
} co_array_t;

co_array_t *co_array_create()
{
    co_array_t *array = (co_array_t *)malloc(sizeof(co_array_t));
    array->len = 0, array->cap = 1;
    array->data =
        (co_array_value_t *)malloc(sizeof(co_array_value_t) * array->cap);
    return array;
}
void co_array_destroy(co_array_t *arr) { 
    for (int i = 0; i < arr->len; ++i) free(arr->data[i]);
    free(arr->data), free(arr); 
}
void co_array_add(co_array_t *arr, co_array_value_t value)
{
    if (arr->len == arr->cap)
    {
        arr->cap <<= 1;
        co_array_value_t *new =
            (co_array_value_t *)malloc(sizeof(co_array_value_t) * arr->cap);
        for (int i = 0; i < arr->len; ++i)
            new[i] = arr->data[i];
        free(arr->data);
        arr->data = new;
    }
    arr->data[arr->len++] = value;
}
#define co_array_set(_arr, _idx, _val) (_arr->data[_idx])
#define co_array_get(_arr, _idx, _typ) ((_typ) _arr->data[_idx])

/* Implementation of Corotine  */

typedef struct co_meta_t co_meta_t;
typedef struct co_struct_t co_struct_t;
typedef struct co_scheduler_t co_scheduler_t;

// meta information of routines PER THREAD
struct co_meta_t {
    pthread_t tid;
        // thread id
    co_struct_t *running; 
        // point to the current running routine (user-created one), 
        // NULL if main routine is running 
    ucontext_t main_uc;
        // ucontext of the main routine
};

#define STACK_SIZE SIGSTKSZ

// task structure of a USER-CREATED routine
struct co_struct_t {
    cid_t cid;
    pthread_t tid;
        // routine&thread id
    co_struct_t *parent;
        // point to the parent routine (user-created one),
        // NULL if the parent routine is main
    void *stack;
        // stack space for this routine
        // binded by ucontext library functions
    ucontext_t uc;
        // uncontext of this routine
        // recorded by ucontext library functions
    co_status_t status;
        // running status of this routine
        // can either be RUNNING or FINISHED
    co_ret_t ret;
        // return val of this routine
    co_lock_t lock;
        // a rwlock of this routine
        // used to control concurrent R/W of ret&status
};

// scheduler of all coroutines
struct co_scheduler_t {
    co_array_t *tinfo;
    co_lock_t tinfo_lock;
        // list of thread meta information and its rwlock
    co_array_t *cinfo;
    co_lock_t cinfo_lock;
        // list of routine information and its rwlock
};

co_lock_t _co_scheduler_lock = PTHREAD_RWLOCK_INITIALIZER;
co_scheduler_t *_co_scheduler;

#define _tinfo _co_scheduler->tinfo
#define _tinfo_lock _co_scheduler->tinfo_lock
#define _cinfo _co_scheduler->cinfo 
#define _cinfo_lock _co_scheduler->cinfo_lock 
#define _thread_id pthread_self()

void co_scheduler_init() {
    RDLOCK(&_co_scheduler_lock);
    if (_co_scheduler == NULL) {
        UNLOCK(&_co_scheduler_lock);
        WRLOCK(&_co_scheduler_lock);
        if (_co_scheduler == NULL) {
            _co_scheduler = (co_scheduler_t *) malloc(sizeof(co_scheduler_t));
            INITLOCK(&_tinfo_lock, NULL);
            INITLOCK(&_cinfo_lock, NULL);
            _tinfo = co_array_create();
            _cinfo = co_array_create();
        }
    }
    UNLOCK(&_co_scheduler_lock);
}

void co_scheduler_destroy() {
    RDLOCK(&_co_scheduler_lock);
    if (_co_scheduler != NULL) {
        UNLOCK(&_co_scheduler_lock);
        WRLOCK(&_co_scheduler_lock);
        if (_co_scheduler != NULL) {
            WRLOCK(&_tinfo_lock);
            WRLOCK(&_cinfo_lock);
            co_array_destroy(_tinfo);
            co_array_destroy(_cinfo);
            UNLOCK(&_tinfo_lock);
            UNLOCK(&_cinfo_lock);
            free(_co_scheduler);
        }
    }
    UNLOCK(&_co_scheduler_lock);
}

co_meta_t* _co_getmeta() {
    co_meta_t *ret = NULL;
    for (int i = 0; i < _tinfo->len; ++i) {
        co_meta_t *cur = co_array_get(_tinfo, i, co_meta_t*);
        // tid won't change, thus no lock is needed
        if (pthread_equal(cur->tid, _thread_id)) {
            ret = cur; break;
        }
    }
    return ret;
}

int co_getid() {
    co_struct_t *coro = _co_getmeta()->running;
    return coro != NULL? coro->cid: -1;
}

// a wrapper is needed to record the return values of routines 
void _co_func_wrapper(co_struct_t *coro, co_func_t func) {
    co_ret_t ret = func();
// printf("[dbg] finish cid %d\n", coro->cid);

    // it is here where routines are actually finished
    WRLOCK(&coro->lock);
    coro->status = FINISHED;
    coro->ret = ret;
    co_meta_t *meta = _co_getmeta();
    assert(meta != NULL);
    meta->running = coro->parent;
    free(coro->stack);
// printf("[dbg] give back to %p\n", coro->parent);
    UNLOCK(&coro->lock);
}

int co_start(co_func_t routine) {
    // check if scheduler is initialized
    co_scheduler_init();

    // create a new corotine structure
    co_struct_t *new_struct = (co_struct_t *)malloc(sizeof(co_struct_t));

    WRLOCK(&_cinfo_lock);
    WRLOCK(&_tinfo_lock);

    new_struct->tid = _thread_id;
    new_struct->status = RUNNING;
    new_struct->ret = -1;
    INITLOCK(&new_struct->lock, NULL);

    new_struct->cid = _cinfo->len;
    co_array_add(_cinfo, new_struct);
// printf("[dbg] start cid %d\n", new_struct->cid);

    co_meta_t* meta = _co_getmeta();
    // if current thread doesn't have its thread meta 
    // information entry, created one 
    if (meta == NULL) {
        co_meta_t *new_meta = (co_meta_t*) malloc(sizeof(co_meta_t));
        co_array_add(_tinfo, new_meta);
        new_meta->tid = _thread_id;
        new_meta->running = NULL;
        meta = new_meta;
// printf("[dbg] main, ucontex %p\n", &meta->main_uc);
    }

    UNLOCK(&_cinfo_lock);
    UNLOCK(&_tinfo_lock);

    new_struct->parent = meta->running;
// printf("[dbg] parent %p\n", new_struct->parent);
// printf("[dbg] cid %d, ucontext %p\n", new_struct->cid, &new_struct->uc);

    // user-created routine as parent
    if (meta->running != NULL)
        new_struct->uc.uc_link = &meta->running->uc;
    // system-created routine as parent
    else
        new_struct->uc.uc_link = &meta->main_uc;
    meta->running = new_struct;

    // initalize corotine context
    if (getcontext(&new_struct->uc) < 0) return -1;
    new_struct->stack = malloc(STACK_SIZE);
    new_struct->uc.uc_stack.ss_sp = new_struct->stack;
    new_struct->uc.uc_stack.ss_size = STACK_SIZE;
    makecontext(&new_struct->uc, 
        (void (*)(void))_co_func_wrapper, 2, new_struct, routine);

    // save current context in uc_ret,
    // and start coroutine with context uc_cur
    if (swapcontext(new_struct->uc.uc_link, &new_struct->uc) < 0) return -1;

    // return cid
    return new_struct->cid;
}

int co_yield() {
    // check if scheduler is initialized
    co_scheduler_init();
    
    RDLOCK(&_tinfo_lock);
    // get metainfo for the current thread 
    co_meta_t *meta = _co_getmeta(); 
    assert(meta != NULL);
    UNLOCK(&_tinfo_lock);

    ucontext_t *suspend_ucp, *resume_ucp;

    int lower_bound;
    // to suspend a user-created routine,
    // search routines with a greater cid
    if (meta->running != NULL) {
        lower_bound = meta->running->cid + 1;
        suspend_ucp = &meta->running->uc;
    }
    // to suspend a system-created routine,
    // search all routines
    else {
        lower_bound = 0;
        suspend_ucp = &meta->main_uc;
    }

    RDLOCK(&_cinfo_lock);
    // main routine as fallback
    resume_ucp = &meta->main_uc;
    meta->running = NULL;
    for (int i = lower_bound; i < _cinfo->len; ++i) {
        co_struct_t *tmp = co_array_get(_cinfo, i, co_struct_t*);
        // no lock is needed for read status,
        // since status can only be modified from in the same thread,
        // and the current thread is taken up for resume routine searching
        co_status_t status = tmp->status;
        if (pthread_equal(tmp->tid, _thread_id) && tmp->status == RUNNING) {
            // a feasible routine is found, resume it
            meta->running = tmp;
            resume_ucp = &tmp->uc;
            break;
        }
    }
    UNLOCK(&_cinfo_lock);

// printf("[dbg] swtich from %p to %p\n", suspend_ucp, resume_ucp);

    // only swap context if we're actually swtich to a 
    // different routine (when yielding from main to main, 
    // no swtich is needed)
    if (suspend_ucp != resume_ucp) {
        if (swapcontext(suspend_ucp, resume_ucp) < 0) return -1;
    }
    return 0;
}

int co_getret(int cid) {
    // check if scheduler is initialized
    co_scheduler_init();

    RDLOCK(&_cinfo_lock);
    co_struct_t *qcoro = co_array_get(_cinfo, cid, co_struct_t*);
    UNLOCK(&_cinfo_lock);
    
    while(1) { 
        RDLOCK(&qcoro->lock);
        co_status_t status = qcoro->status;
        co_ret_t ret = qcoro->ret;
        UNLOCK(&qcoro->lock);
        if (status == FINISHED) return ret;
        else co_yield();
    }
    return 0;
}

// get UNAUTHORIZED when:
// 1. invalid cid is provided
// 2. two routines aren't in the same thread
// 3. current routine isn't a PARENT or ANCESTOR of queried routine
int co_status(int cid) {
    // check if scheduler is initialized
    co_scheduler_init();

    RDLOCK(&_tinfo_lock);
    co_meta_t *meta = _co_getmeta();
    UNLOCK(&_tinfo_lock);

    RDLOCK(&_cinfo_lock);
    if (cid < 0 || cid > _cinfo->len) {
        UNLOCK(&_cinfo_lock); return UNAUTHORIZED;
    }
    co_struct_t *qcoro = co_array_get(_cinfo, cid, co_struct_t*);
    UNLOCK(&_cinfo_lock);

    if (pthread_equal(qcoro->tid, _thread_id) && meta != NULL) {
        if (meta->running == NULL) return qcoro->status;
        co_struct_t *coro = qcoro;
        for (; coro != NULL; coro = coro->parent) {
            if (coro == meta->running) return qcoro->status;
        }
    }
    return UNAUTHORIZED;
}

int co_wait(int cid) {
    // check if scheduler is initialized
    co_scheduler_init();
    
    RDLOCK(&_tinfo_lock);
    co_meta_t *meta = _co_getmeta();
    UNLOCK(&_tinfo_lock);

    RDLOCK(&_cinfo_lock);
    co_struct_t *qcoro = co_array_get(_cinfo, cid, co_struct_t*);
    UNLOCK(&_cinfo_lock);

    while(1) {
        RDLOCK(&qcoro->lock);
        co_status_t status = qcoro->status;
        UNLOCK(&qcoro->lock);
        if (status == FINISHED) return 0;
        else co_yield(); 
    }
    return 0;
}

int co_waitall() {
    // check if scheduler is initialized
    co_scheduler_init();

    while (1) {
        RDLOCK(&_cinfo_lock);
        int all_finished = 1;
        for (int i = 0; i < _cinfo->len; ++i) {
            co_struct_t *tmp = co_array_get(_cinfo, i, co_struct_t*);
            RDLOCK(&tmp->lock);
            if (tmp->status != FINISHED) {
                all_finished = 0;
                UNLOCK(&tmp->lock);
                break;
            }
            UNLOCK(&tmp->lock);
        }
        UNLOCK(&_cinfo_lock);
        if (all_finished) return 0;
        else co_yield();
    }
    return 0;
}