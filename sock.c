#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <linux/sched.h>
#include <errno.h>
#include <assert.h>
#include "erl_nif.h"

#define INVALID_SOCKET -1

typedef struct _namespace_t {
    struct _namespace_t  *next;
    int             ns;
    int             slots;
    ERL_NIF_TERM    socklist;
    ErlNifPid       *pid;
} namespace_t;

typedef struct {
    ErlNifMutex     *lock;
    ErlNifCond      *cond;
    namespace_t     *head;
    namespace_t     *tail;
} queue_t;

typedef struct {
    ErlNifThreadOpts*   opts;
    ErlNifTid           qthread;
    queue_t             *queue;
    ERL_NIF_TERM        atom_ok;
} state_t;


static ERL_NIF_TERM makesocks(ErlNifEnv*, namespace_t*);
namespace_t *popns(queue_t*);
static void *thr_main(void*);

static queue_t*
queue_create() {
    queue_t* ret;

    ret = (queue_t*) enif_alloc(sizeof(queue_t));
    if(ret == NULL) return NULL;

    ret->lock = NULL;
    ret->cond = NULL;
    ret->head = NULL;
    ret->tail = NULL;

    ret->lock = enif_mutex_create("queue_lock");
    if(ret->lock == NULL) goto error;

    ret->cond = enif_cond_create("queue_cond");
    if(ret->cond == NULL) goto error;

    return ret;

error:
    if(ret->lock != NULL) enif_mutex_destroy(ret->lock);
    if(ret->cond != NULL) enif_cond_destroy(ret->cond);
    if(ret != NULL) enif_free(ret);
    return NULL;
}

void
queue_destroy(queue_t* queue)
{
    ErlNifMutex* lock;
    ErlNifCond* cond;

    enif_mutex_lock(queue->lock);
    assert(queue->head == NULL && "Destroying a non-empty queue.");
    assert(queue->tail == NULL && "Destroying queue in invalid state.");

    lock = queue->lock;
    cond = queue->cond;

    queue->lock = NULL;
    queue->cond = NULL;

    enif_mutex_unlock(lock);

    enif_cond_destroy(cond);
    enif_mutex_destroy(lock);
    enif_free(queue);
}

static void*
thr_main(void *obj)
{
    state_t *state = (state_t*) obj;
    ErlNifEnv* env = enif_alloc_env();
    namespace_t     *ns;
    ERL_NIF_TERM res = 0;

    while ((ns = popns(state->queue)) != NULL)
    {
        res = makesocks(env, ns);
        enif_send(NULL, ns->pid, env, res);
        enif_free(ns->pid);
        enif_free(ns);
        enif_clear_env(env);
    }
    return NULL;
}

namespace_t*
popns(queue_t *queue)
{
    namespace_t *item;

    enif_mutex_lock(queue->lock);

    while (queue->head == NULL)
        enif_cond_wait(queue->cond, queue->lock);

    item = queue->head;
    queue->head = item->next;
    item->next = NULL;

    if (queue->head == NULL)
        queue->tail = NULL;

    enif_mutex_unlock(queue->lock);
    return item;
}

int
addns(queue_t *queue, ErlNifPid *pid, int slots, int ns)
{
    namespace_t *nns = (namespace_t*) enif_alloc(sizeof(namespace_t));
    if (nns == NULL) return 0;

    nns->ns = ns;       /* Store namespace to alloc sockets in */
    nns->slots = slots; /* Store no of slots to allocate */
    nns->pid = pid;     /* Store pid to send reply back to */
    nns->next = NULL;

    enif_mutex_lock(queue->lock);

    if(queue->tail != NULL)
    {
        queue->tail->next = nns;
    }

    queue->tail = nns;

    if(queue->head == NULL)
    {
        queue->head = queue->tail;
    }

    enif_cond_signal(queue->cond);
    enif_mutex_unlock(queue->lock);

    return 1;
}

static int
load(ErlNifEnv* env, void** priv, ERL_NIF_TERM load_info)
{
    state_t *state = (state_t*)enif_alloc(sizeof(state_t));
    if (state == (state_t*)-1) return -1;

    state->queue = queue_create();
    if (state->queue == NULL) goto error;

    state->opts = enif_thread_opts_create("thread_opts");
    if(enif_thread_create("", &(state->qthread), thr_main,
                          (void*)state, state->opts) != 0)
    {
        return -1;
    }

    state->atom_ok = enif_make_atom(env, "ok");
    *priv = (void*) state;
    return 0;

error:
    if(state->queue != NULL) queue_destroy(state->queue);
    enif_free(state->queue);
    return -1;
}

// ----------------------------------------------------------------------------
// The actual C implementation of an Erlang function.
//
// Docs: http://erlang.org/doc/man/erl_nif.html

static ERL_NIF_TERM
sock(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    state_t* state = (state_t*) enif_priv_data(env);
    ErlNifPid* pid = (ErlNifPid*) enif_alloc(sizeof(ErlNifPid));
    int slots = 0, ns = 0;

    if(!enif_get_local_pid(env, argv[0], pid)) {
        return enif_make_badarg(env);
    }

    if (! enif_get_int(env, argv[1], &slots)) {
        return enif_make_badarg(env);
    }

    if (! enif_get_int(env, argv[2], &ns)) {
        return enif_make_badarg(env);
    }

    addns(state->queue, pid, slots, ns);
    return state->atom_ok;
}

static ERL_NIF_TERM
makesocks(ErlNifEnv *env, namespace_t *ns)
{
    int slots = ns->slots;
    int fd, i = 0;
    int current_ns, new_ns;
    char netns[30];
    ERL_NIF_TERM list, res, tail;

    current_ns = new_ns = 0;

    if (ns->ns != 0) {
        sprintf(&netns[0], "/var/run/netns/%d", ns->ns);
        //sprintf(&netns[0], "/var/run/netns/topi");
        current_ns = open("/proc/self/ns/net", O_RDONLY);
        if (current_ns == INVALID_SOCKET)
           return enif_make_atom(env, "currnserror");

        new_ns = open(netns, O_RDONLY);
        if (new_ns == INVALID_SOCKET) {
            close(current_ns);
            return enif_make_atom(env, "newnserror");
        }
        if (setns(new_ns, CLONE_NEWNET) != 0) {
            close(new_ns);
            close(current_ns);
            return enif_make_atom(env, "setnserror");
        } else {
            close(new_ns);
        }
    }

    list = enif_make_list(env, 0);
    for (i; i < slots; i++) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd != -1) {
            tail = enif_make_int(env, fd);
            list = enif_make_list_cell(env, tail, list);
        }
        else
            ;
    }

    if (ns->ns != 0) {
        if (setns(current_ns, CLONE_NEWNET) != 0) {
            /* major error */
            close(current_ns);
            return;
        } else {
            close(current_ns);
        }
    }

    res = ns->socklist = list;
    return res;
}

//static ERL_NIF_TERM
//sock(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
//{
 //   int slots = 0;
//    int fd, i = 0;
//    ERL_NIF_TERM list, res, tail;
//
//    if (! enif_get_int(env, argv[0], &slots)) {
//        return enif_make_badarg(env);
//    }
//
//    list = enif_make_list(env, 0);
//    for (i; i < slots; i++) {
//        fd = socket(AF_INET, SOCK_STREAM, 0);
//        if (fd != -1) {
//            tail = enif_make_int(env, fd);
//            list = enif_make_list_cell(env, tail, list);
//        }
//        else
//            return enif_make_atom(env, "socket_failure");
//    }

    // FIXME return {ok, List}
//    return list;
//}

static ERL_NIF_TERM
closesock(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    int fd;
    int errsv;

    if (! enif_get_int(env, argv[0], &fd)) {
        return enif_make_badarg(env);
    }

   if (close(fd) < 0) {
      return enif_make_atom(env, "eio");
   }

   return enif_make_atom(env, "ok");
}

static ErlNifFunc nif_funcs[] = {
    {"sock", 3, sock},
    {"closesock", 1, closesock}
};

static int
upgrade(ErlNifEnv* env, void** priv, void** old_priv, ERL_NIF_TERM load_info)
{
    return;
}

static void
unload(ErlNifEnv* env, void* priv)
{
    state_t* state = (state_t*) priv;
    enif_thread_opts_destroy(state->opts);
    enif_free(state);
    return;
}

// Initialize this NIF library.
ERL_NIF_INIT(sysSock, nif_funcs, &load, NULL, &upgrade, &unload);

