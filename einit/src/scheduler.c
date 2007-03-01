/*
 *  scheduler.c
 *  einit
 *
 *  Created by Magnus Deininger on 02/05/2006.
 *  Copyright 2006, 2007 Magnus Deininger. All rights reserved.
 *
 */

/*
Copyright (c) 2006, 2007, Magnus Deininger
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
    * Neither the name of the project nor the names of its contributors may be
	  used to endorse or promote products derived from this software without
	  specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <einit/scheduler.h>
#include <einit/config.h>
#include <einit/module.h>
#include <einit/utility.h>
#include <einit/bitch.h>
#include <einit/event.h>
#include <signal.h>
#include <errno.h>
#include <einit/module-logic.h>
#include <semaphore.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

pthread_mutex_t schedcpidmutex = PTHREAD_MUTEX_INITIALIZER;

sem_t *sigchild_semaphore = NULL;

#if ((_POSIX_SEMAPHORES - 200112L) >= 0)
sem_t sigchild_semaphore_static;
#endif

stack_t signalstack;

struct spidcb *cpids = NULL;
struct spidcb *sched_deadorphans = NULL;
uint32_t gstatus = EINIT_NOMINAL;

char sigint_called = 0;

int cleanup ();

int scheduler_cleanup () {
 sem_t *sembck = sigchild_semaphore;
 stack_t curstack;
 sigchild_semaphore = NULL;

 if (!sigaltstack (NULL, &curstack) && !(curstack.ss_flags & SS_ONSTACK)) {
  curstack.ss_size = SIGSTKSZ;
  curstack.ss_flags = SS_DISABLE;
  sigaltstack (&curstack, NULL);
//  free (curstack.ss_sp);
 } else {
  notice (1, "schedule: no alternate signal stack or alternate stack in use; not cleaning up");
 }

#if ((_POSIX_SEMAPHORES - 200112L) >= 0)
 sem_destroy (sembck);
// free (sembck);
#elif defined(DARWIN)
 sem_close (sembck);
#else
 if (sem_destroy (sembck))
  sem_close (sembck);
#endif

 return 0;
}

void sched_init () {
#if ((_POSIX_SEMAPHORES - 200112L) >= 0)
 sigchild_semaphore = &sigchild_semaphore_static;
 sem_init (sigchild_semaphore, 0, 0);
#elif defined(DARWIN)
 char tmp[BUFFERSIZE];

 esprintf (tmp, BUFFERSIZE, "/einit-sgchld-sem-%i", getpid());

 if ((int)(sigchild_semaphore = sem_open (tmp, O_CREAT, O_RDWR, 0)) == SEM_FAILED) {
  perror ("scheduler: semaphore setup");
  exit (EXIT_FAILURE);
 }
#else
#warning no proper or recognised semaphores implementation, i can't promise this code will work.
/* let's just hope for the best... */
 char tmp[BUFFERSIZE];

 sigchild_semaphore = ecalloc (1, sizeof (sem_t));
 if (sem_init (sigchild_semaphore, 0, 0) == -1) {
  free (sigchild_semaphore);
  esprintf (tmp, BUFFERSIZE, "/einit-sigchild-semaphore-%i", getpid());

  if ((sigchild_semaphore = sem_open (tmp, O_CREAT, O_RDWR, 0)) == SEM_FAILED) {
   perror ("scheduler: semaphore setup");
   exit (EXIT_FAILURE);
  }
 }
#endif

// pthread_create (&schedthreadsigchild, &thread_attribute_detached, sched_run_sigchild, NULL);

 sched_reset_event_handlers ();
}

void sched_reset_event_handlers () {
 struct sigaction action;

 signalstack.ss_sp = emalloc (SIGSTKSZ);
 signalstack.ss_size = SIGSTKSZ;
 signalstack.ss_flags = 0;
 sigaltstack (&signalstack, NULL);

 sigemptyset(&(action.sa_mask));

/* signal handlers */
 action.sa_sigaction = sched_signal_sigchld;
// action.sa_flags = SA_NOCLDSTOP | SA_SIGINFO | SA_RESTART | SA_NODEFER | SA_ONSTACK;
 action.sa_flags = SA_NOCLDSTOP | SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
// SA_NODEFER should help with a waitpid()-race... and since we don't do any locking in the handler anymore...
 if ( sigaction (SIGCHLD, &action, NULL) ) bitch (BTCH_ERRNO);

 action.sa_flags = SA_SIGINFO | SA_RESTART | SA_NODEFER | SA_ONSTACK;
 action.sa_sigaction = sched_signal_sigint;
 if ( sigaction (SIGINT, &action, NULL) ) bitch (BTCH_ERRNO);

/* ignore most signals */
 action.sa_sigaction = (void (*)(int, siginfo_t *, void *))SIG_IGN;

 if ( sigaction (SIGQUIT, &action, NULL) ) bitch (BTCH_ERRNO);
 if ( sigaction (SIGABRT, &action, NULL) ) bitch (BTCH_ERRNO);
 if ( sigaction (SIGPIPE, &action, NULL) ) bitch (BTCH_ERRNO);
// if ( sigaction (SIGALRM, &action, NULL) ) bitch (BTCH_ERRNO);
 if ( sigaction (SIGUSR1, &action, NULL) ) bitch (BTCH_ERRNO);
 if ( sigaction (SIGUSR2, &action, NULL) ) bitch (BTCH_ERRNO);
 if ( sigaction (SIGTSTP, &action, NULL) ) bitch (BTCH_ERRNO);
 if ( sigaction (SIGTTIN, &action, NULL) ) bitch (BTCH_ERRNO);
 if ( sigaction (SIGTTOU, &action, NULL) ) bitch (BTCH_ERRNO);
 if (gmode != EINIT_GMODE_SANDBOX) {
  if ( sigaction (SIGTERM, &action, NULL) ) bitch (BTCH_ERRNO);
 }
#ifdef SIGPOLL
 if ( sigaction (SIGPOLL, &action, NULL) ) bitch (BTCH_ERRNO);
#endif
 if ( sigaction (SIGPROF, &action, NULL) ) bitch (BTCH_ERRNO);
// if ( sigaction (SIGVTALRM, &action, NULL) ) bitch (BTCH_ERRNO);
 if ( sigaction (SIGXCPU, &action, NULL) ) bitch (BTCH_ERRNO);
 if ( sigaction (SIGXFSZ, &action, NULL) ) bitch (BTCH_ERRNO);
#ifdef SIGIO
 if ( sigaction (SIGIO, &action, NULL) ) bitch (BTCH_ERRNO);
#endif

}

int sched_watch_pid (pid_t pid, void *(*function)(struct spidcb *)) {
 struct spidcb *nele;
 emutex_lock (&schedcpidmutex);
#ifdef BUGGY_PTHREAD_CHILD_WAIT_HANDLING
  if (sched_deadorphans) {
   struct spidcb *start = sched_deadorphans, *prev = NULL, *cur = start;
   for (; cur; cur = cur->next) {
    if (cur->pid == (pid_t)pid) {
     cur->cfunc = function;
     if (prev)
      prev->next = cur->next;
     else
      sched_deadorphans = cur->next;
     cur->next = cpids;
     cpids = cur;
     emutex_unlock (&schedcpidmutex);
     return 0;
    }
    if (start != sched_deadorphans) {
     cur = sched_deadorphans;
     start = cur;
     prev = NULL;
    } else
     prev = cur;
   }
  }
#endif
  nele = ecalloc (1, sizeof (struct spidcb));
  nele->pid = pid;
  nele->cfunc = function;
  nele->dead = 0;
  nele->status = 0;
  nele->next = cpids;
 cpids = nele;

 emutex_unlock (&schedcpidmutex);
 if ((gstatus != EINIT_EXITING) && sigchild_semaphore)
  sem_post (sigchild_semaphore);

 return 0;
}

// (on linux) SIGINT to INIT means ctrl+alt+del was pressed
void sched_signal_sigint (int signal, siginfo_t *siginfo, void *context) {
#ifdef LINUX
/* only shut down if the SIGINT was sent by the kernel, (e)INIT (process 1) or by the parent process */
 if ((siginfo->si_code == SI_KERNEL) ||
     (((siginfo->si_code == SI_USER) && (siginfo->si_pid == 1)) || (siginfo->si_pid == getppid())))
#else
/* only shut down if the SIGINT was sent by process 1 or by the parent process */
/* if ((siginfo->si_pid == 1) || (siginfo->si_pid == getppid())) */
// note: this relies on a proper pthreads implementation so... i deactivated it for now.
#endif
 {
  sigint_called = 1;
  if ((gstatus != EINIT_EXITING) && sigchild_semaphore)
   sem_post (sigchild_semaphore);

 }
 return;
}

void sched_ipc_event_handler(struct einit_event *ev) {
 errno = 0;
 if (!ev) return;
 else {
  char **argv = (char **)ev->set;
  int argc = setcount ((void **)argv);
  if (!argv) {
   perror ("sched_ipc_event_handler: counting argv");
   return;
  } else if (!argv[0]) {
   free (argv);
   perror ("sched_ipc_event_handler: basic sanity check");
   return;
  }

  if (!strcmp (argv[0], "power") && (argc > 1)) {
   if (!strcmp (argv[1], "down") || !strcmp (argv[1], "off")) {
    if (!ev->flag) ev->flag = 1;

    struct einit_event ee = evstaticinit(EVE_SWITCH_MODE);
    ee.string = "power-down";
    event_emit (&ee, EINIT_EVENT_FLAG_SPAWN_THREAD | EINIT_EVENT_FLAG_DUPLICATE | EINIT_EVENT_FLAG_BROADCAST);
    eputs (" >> shutdown queued\n", (FILE *)ev->para);
    evstaticdestroy(ee);
   }
   if (!strcmp (argv[1], "reset")) {
    if (!ev->flag) ev->flag = 1;

    struct einit_event ee = evstaticinit(EVE_SWITCH_MODE);
    ee.string = "power-reset";
    event_emit (&ee, EINIT_EVENT_FLAG_SPAWN_THREAD | EINIT_EVENT_FLAG_DUPLICATE | EINIT_EVENT_FLAG_BROADCAST);
    eputs (" >> reset queued\n", (FILE *)ev->para);
    evstaticdestroy(ee);
   }
  }

/* actual power-down/power-reset IPC commands */
  if (!strcmp (argv[0], "scheduler") && (argc > 1)) {
   char reset = 0;
   if (!strcmp (argv[1], "power-down") || (reset = !strcmp (argv[1], "power-reset"))) {
    if (!ev->flag) ev->flag = 1;

     notice (1, ">> scheduler: sync()-ing");

     sync ();

     if (gmode == EINIT_GMODE_SANDBOX) {
      notice (1, ">> scheduler: cleaning up");
     }

     gstatus = EINIT_EXITING;
     if (sigchild_semaphore)
      sem_post (sigchild_semaphore);

     char **shutdownfunctionsubnames = str2set (':', cfg_getstring ("core-scheduler-shutdown-function-suffixes", NULL));

     void  ((**reset_functions)()) = (void (**)())
      (shutdownfunctionsubnames ? function_find((reset ? "core-power-reset" : "core-power-off"), 1, shutdownfunctionsubnames) : NULL);

     eputs ((reset ? "scheduler: reset\n" : "scheduler: power down\n"), stderr);

     if (reset_functions) {
      uint32_t xn = 0;

      for (; reset_functions[xn]; xn++) {
       (reset_functions[xn]) ();
      }
     } else {
      eputs ("scheduler: no (accepted) functions found, exiting\n", stderr);
      exit (EXIT_SUCCESS);
     }

     if (shutdownfunctionsubnames) free (shutdownfunctionsubnames);

// if we still live here, something's twocked
     eputs ("scheduler: failed, exiting\n", stderr);
     exit (EXIT_FAILURE);
   }
  }

  if (!strcmp (argv[0], "rc") && (argc > 2)) {
   if (!ev->flag) ev->flag = 1;

   if (!strcmp (argv[1], "switch-mode")) {
    struct einit_event ee = evstaticinit(EVE_SWITCH_MODE);
    ee.string = argv[2];
    if (ev->status & EIPC_DETACH) {
     event_emit (&ee, EINIT_EVENT_FLAG_SPAWN_THREAD | EINIT_EVENT_FLAG_DUPLICATE | EINIT_EVENT_FLAG_BROADCAST);
     eputs (" >> modeswitch queued\n", (FILE *)ev->para);
    } else {
     ee.para = ev->para;
     event_emit (&ee, EINIT_EVENT_FLAG_BROADCAST);
     ev->integer = ee.integer;
    }
    evstaticdestroy(ee);
   } else {
    struct einit_event ee = evstaticinit(EVE_CHANGE_SERVICE_STATUS);
    ee.set = (void **)setdup ((void **)argv+1, SET_TYPE_STRING);
    if (ev->status & EIPC_DETACH) {
     event_emit (&ee, EINIT_EVENT_FLAG_SPAWN_THREAD | EINIT_EVENT_FLAG_DUPLICATE | EINIT_EVENT_FLAG_BROADCAST);
     eputs (" >> status change queued\n", (FILE *)ev->para);
    } else {
     ee.para = ev->para;
     event_emit (&ee, EINIT_EVENT_FLAG_BROADCAST);
     ev->integer = ee.integer;
    }
    evstaticdestroy(ee);
   }
  }

  if (errno) {
#ifdef DEBUG
   perror ("sched_ipc_event_handler: cleanup sanity check");
#endif
   errno = 0;
  }
 }
}

#ifdef EINIT_MODULES_MODULE_LOGIC_V2
int sched_switchmode (char *mode) {
 if (!mode) return -1;
 struct cfgnode *cur = cfg_findnode (mode, EI_NODETYPE_MODE, NULL);
 struct mloadplan *plan = NULL;

  if (!cur) {
   notice (1, "scheduler: scheduled mode not defined, aborting");
   return -1;
  }

  plan = mod_plan (NULL, NULL, 0, cur);
  if (!plan) {
   notice (1, "scheduler: scheduled mode defined but nothing to be done");
  } else {
   pthread_t th;
   mod_plan_commit (plan);
/* make it so that the erase operation will not disturb the flow of the program */
   ethread_create (&th, &thread_attribute_detached, (void *(*)(void *))mod_plan_free, (void *)plan);
  }

 return 0;
}

int sched_modaction (char **argv) {
 int argc = setcount ((void **)argv), ret = 1;
 int32_t task = 0;
 struct mloadplan *plan;
 if (!argv || (argc != 2)) return -1;

 if (!strcmp (argv[1], "enable")) task = MOD_ENABLE;
 else if (!strcmp (argv[1], "disable")) task = MOD_DISABLE;
 else if (!strcmp (argv[1], "reset")) task = MOD_RESET;
 else if (!strcmp (argv[1], "reload")) task = MOD_RELOAD;
 else if (!strcmp (argv[1], "zap")) task = MOD_ZAP;

 argv[1] = NULL;

 if ((plan = mod_plan (NULL, argv, task, NULL))) {
  pthread_t th;

  ret = mod_plan_commit (plan);

#ifdef MODULE_LOGIC_V2
/* extra code needed for v2 logic */

  if (plan->services) {
   struct mloadplan_node *node = (struct mloadplan_node *)streefind (plan->services, argv[0], TREE_FIND_FIRST);

   if (node) switch (task) {
    case MOD_ENABLE:
     ret = !(node->status & STATUS_ENABLED); break;
    case MOD_DISABLE:
     ret = !(node->status & STATUS_DISABLED); break;
    case MOD_RESET:
     ret = !(node->status & STATUS_ENABLED); break;
    case MOD_RELOAD:
     ret = !(node->status & STATUS_ENABLED); break;
    case MOD_ZAP:
     ret = !(node->status == STATUS_IDLE); break;
   }
  }
#endif

  ethread_create (&th, &thread_attribute_detached, (void *(*)(void *))mod_plan_free, (void *)plan);
 }

// free (argv[0]);
// free (argv);

 return ret;
}

void sched_core_event_handler(struct einit_event *ev) {
 if (!ev) return;
 switch (ev->type) {
  case EVE_SWITCH_MODE:
   if (!ev->string) return;
   else {
    if (ev->para) {
     struct einit_event ee = evstaticinit(EVENT_FEEDBACK_REGISTER_FD);
     ee.para = ev->para;
     event_emit (&ee, EINIT_EVENT_FLAG_BROADCAST);
     evstaticdestroy(ee);
    }

    sched_switchmode (ev->string);

    if (ev->para) {
     struct einit_event ee = evstaticinit(EVENT_FEEDBACK_UNREGISTER_FD);
     ee.para = ev->para;
     event_emit (&ee, EINIT_EVENT_FLAG_BROADCAST);
     evstaticdestroy(ee);
    }
   }
   return;
  case EVE_CHANGE_SERVICE_STATUS:
   if (!ev->set) return;
   else {
    if (ev->para) {
     struct einit_event ee = evstaticinit(EVENT_FEEDBACK_REGISTER_FD);
     ee.para = ev->para;
     event_emit (&ee, EINIT_EVENT_FLAG_BROADCAST);
     evstaticdestroy(ee);
    }

    if (sched_modaction ((char **)ev->set)) {
     ev->integer = 1;
    }

    if (ev->para) {
     struct einit_event ee = evstaticinit(EVENT_FEEDBACK_UNREGISTER_FD);
     ee.para = ev->para;
     event_emit (&ee, EINIT_EVENT_FLAG_BROADCAST);
     evstaticdestroy(ee);
    }
   }
   return;
 }
}
#endif

/* BUG: linux pthread-libraries on kernel <= 2.4 can not wait on other threads' children, thus
        when doing a ./configure, you have to specify the option --pthread-wait-bug. these functions
        are buggy too, though, and subject to racing bugs. */
#ifdef BUGGY_PTHREAD_CHILD_WAIT_HANDLING
void *sched_run_sigchild (void *p) {
 int i, l, status;
 pid_t pid;
 int check;
 while (1) {
  emutex_lock (&schedcpidmutex);
  struct spidcb *start = cpids, *prev = NULL, *cur = start;
  check = 0;
  for (; cur; cur = cur->next) {
   pid = cur->pid;
   if (cur->dead) {
    check++;

    if (cur->cfunc) {
     pthread_t th;
     ethread_create (&th, &thread_attribute_detached, (void *(*)(void *))cur->cfunc, (void *)cur);
    }

    if (prev)
     prev->next = cur->next;
    else
     cpids = cur->next;

    break;
   }
   if (start != cpids) {
    cur = cpids;
    start = cur;
    prev = NULL;
   } else
    prev = cur;
  }
  emutex_unlock (&schedcpidmutex);
  if (!check) {
   sem_wait (sigchild_semaphore);
   if (gstatus == EINIT_EXITING) {
    return NULL;
   }

   if (sigint_called) {
    struct einit_event ee;
    ee.type = EVE_SWITCH_MODE;
    ee.string = "power-reset";

//    ee.para = stdout;

    event_emit (&ee, EINIT_EVENT_FLAG_SPAWN_THREAD | EINIT_EVENT_FLAG_DUPLICATE | EINIT_EVENT_FLAG_BROADCAST);
//  evstaticdestroy(ee);

    sigint_called = 0;
   }
  }
 }
}

/* signal handlers */

/* apparently some of the pthread*() functions aren't asnyc-safe... still it
   appears to be working, so... */

/* I came up with this pretty cheap solution to prevent deadlocks while still being able to use mutexes */
void *sched_signal_sigchld_addentrythreadfunction (struct spidcb *nele) {
 char known = 0;
 emutex_lock (&schedcpidmutex);
 struct spidcb *cur = cpids;
  for (; cur; cur = cur->next) {
   if (cur->pid == (pid_t)nele->pid) {
    known++;
    cur->status = nele->status;
    cur->dead = 1;
    free (nele);
    break;
   }
  }

  if (!known) {
   nele->cfunc = NULL;
   nele->dead = 1;

   nele->next = sched_deadorphans;
   sched_deadorphans = nele;
  }
 emutex_unlock (&schedcpidmutex);

 sem_post (sigchild_semaphore);
}

/* this should prevent any zombies from being created */
void sched_signal_sigchld (int signal, siginfo_t *siginfo, void *context) {
 int i, status;
 pid_t pid;
 struct spidcb *nele;
 pthread_t th;

 while (pid = waitpid (-1, &status, WNOHANG)) {
  if (pid == -1) {
   break;
  }

  nele = ecalloc (1, sizeof (struct spidcb));
  nele->pid = pid;
  nele->status = status;

  ethread_create (&th, &thread_attribute_detached, (void *(*)(void *))sched_signal_sigchld_addentrythreadfunction, (void *)nele);
 }

 return;
}

#else
void *sched_run_sigchild (void *p) {
 int status, check;
 pid_t pid;
 while (1) {
  emutex_lock (&schedcpidmutex);
  struct spidcb *start = cpids, *prev = NULL, *cur = start;
  check = 0;

  for (; cur; cur = cur->next) {
   pid = cur->pid;
   if ((!cur->dead) && (waitpid (pid, &status, WNOHANG) > 0)) {
    if (WIFEXITED(status) || WIFSIGNALED(status)) cur->dead = 1;
   }

   if (cur->dead) {
    check++;
    if (cur->cfunc) {
     pthread_t th;
     ethread_create (&th, &thread_attribute_detached, (void *(*)(void *))cur->cfunc, (void *)cur);
    }

    if (prev)
     prev->next = cur->next;
    else
     cpids = cur->next;

    break;
   }

   if (start != cpids) {
    cur = cpids;
    start = cur;
    prev = NULL;
   } else
    prev = cur;
  }

  emutex_unlock (&schedcpidmutex);
  if (!check) {
   if (gstatus != EINIT_EXITING) sem_wait (sigchild_semaphore);
   else {
    eputs (" >> scheduler SIGCHLD thread now going to sleep\n", stderr);
    while (sleep (1)) {
     eputs (" >> still not dead...", stderr);
    }
   }
   if (sigint_called) {
    eputs (" >> scheduler SIGCHLD thread: making eINIT shut down\n", stderr);

    struct einit_event ee = evstaticinit (EVE_SWITCH_MODE);
    ee.string = "power-reset";

//    ee.para = stdout;

    event_emit (&ee, EINIT_EVENT_FLAG_SPAWN_THREAD | EINIT_EVENT_FLAG_DUPLICATE | EINIT_EVENT_FLAG_BROADCAST);
//  evstaticdestroy(ee);

    sigint_called = 0;
    evstaticdestroy (ee);
   }
  }
 }
}

void sched_signal_sigchld (int signal, siginfo_t *siginfo, void *context) {
 if ((gstatus != EINIT_EXITING) && sigchild_semaphore)
  sem_post (sigchild_semaphore);

 return;
}
#endif
