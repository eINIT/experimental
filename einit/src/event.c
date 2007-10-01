/*
 *  event.c
 *  eINIT
 *
 *  Created by Magnus Deininger on 25/06/2006.
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

#include <inttypes.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <einit/config.h>
#include <einit/event.h>
#include <einit/utility.h>
#include <einit/tree.h>
#include <einit/bitch.h>
#include <errno.h>

pthread_mutex_t evf_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t pof_mutex = PTHREAD_MUTEX_INITIALIZER;

uint32_t cseqid = 0;

struct wakeup_data {
 enum einit_event_code code;
 struct lmodule *module;
 struct wakeup_data *next;
};

pthread_mutex_t event_wakeup_mutex = PTHREAD_MUTEX_INITIALIZER;

struct wakeup_data *event_wd = NULL;

extern time_t event_snooze_time;

void event_do_wakeup_calls (enum einit_event_code c) {
 struct wakeup_data *d = event_wd;

 while (d) {
  if (d->code == c) {
   struct lmodule *m = NULL;
   emutex_lock (&event_wakeup_mutex);
   if (d->module && (d->module->status & status_suspended))
    m = d->module;
   emutex_unlock (&event_wakeup_mutex);

   if (m) {
    mod(einit_module_resume, m, NULL);
	time_t nt = time(NULL) + 60;
    emutex_lock (&event_wakeup_mutex);
    if (!event_snooze_time || ((event_snooze_time+30) < nt)) {
     event_snooze_time = nt;
     emutex_unlock (&event_wakeup_mutex);

     {
      struct einit_event ev = evstaticinit (einit_timer_set);
      ev.integer = event_snooze_time;

      event_emit (&ev, einit_event_flag_broadcast);

      evstaticdestroy (ev);
     }
    } else
     emutex_unlock (&event_wakeup_mutex);
   }
  }
  d = d->next;
 }
}

struct evt_wrapper_data {
  void (*handler) (struct einit_event *);
  struct einit_event *event;
};

void *event_thread_wrapper (struct evt_wrapper_data *d) {
 if (d) {
  d->handler (d->event);

  evdestroy (d->event);
  free (d);
 }

 return NULL;
}

void *event_subthread (struct einit_event *event) {
 static char recurse = 0;
 if (!event) return NULL;

 uint32_t subsystem = event->type & EVENT_SUBSYSTEM_MASK;

 /* initialise sequence id and timestamp of the event */
 event->seqid = cseqid++;
 event->timestamp = time(NULL);

 struct event_function *cur = event_functions;
 while (cur) {
  if (((cur->type == subsystem) || (cur->type == einit_event_subsystem_any)) && cur->handler) {
   cur->handler (event);
  }
  cur = cur->next;
 }

 if (event->chain_type) {
  event->type = event->chain_type;
  event->chain_type = 0;

  recurse++;
  event_subthread (event);
  recurse--;
 }

 if (!recurse) {
  if (subsystem == einit_event_subsystem_ipc) {
   if (event->argv) free (event->argv);
  } else {
   if (event->stringset) free (event->stringset);
  }

  evdestroy (event);
 }

 return NULL;
}

void *event_emit (struct einit_event *event, enum einit_event_emit_flags flags) {
 pthread_t **threads = NULL;
 if (!event || !event->type) return NULL;

 event_do_wakeup_calls (event->type);

 if (flags & einit_event_flag_spawn_thread) {
  pthread_t threadid;

  struct einit_event *ev = evdup(event);
  if (!ev) return NULL;

  if (ethread_create (&threadid, &thread_attribute_detached, (void *(*)(void *))event_subthread, ev)) {
   event_subthread (ev);
  }

  return NULL;
 }

 uint32_t subsystem = event->type & EVENT_SUBSYSTEM_MASK;

/* initialise sequence id and timestamp of the event */
  event->seqid = cseqid++;
  event->timestamp = time(NULL);

  struct event_function *cur = event_functions;
  while (cur) {
   if (((cur->type == subsystem) || (cur->type == einit_event_subsystem_any)) && cur->handler) {
    if (flags & einit_event_flag_spawn_thread_multi_wait) {
     pthread_t *threadid = emalloc (sizeof (pthread_t));
     struct evt_wrapper_data *d = emalloc (sizeof (struct evt_wrapper_data));

     d->event = evdup(event);
     d->handler = cur->handler;

     ethread_create (threadid, NULL, (void *(*)(void *))event_thread_wrapper, d);
     threads = (pthread_t **)setadd ((void **)threads, threadid, SET_NOALLOC);
    } else
     cur->handler (event);
   }
   cur = cur->next;
  }

 if (event->chain_type) {
  event->type = event->chain_type;
  event->chain_type = 0;
  event_emit (event, flags);
 }

 if ((flags & einit_event_flag_spawn_thread_multi_wait) && threads) {
  int i = 0;

  for (; threads[i]; i++) {
   pthread_join (*(threads[i]), NULL);

   free (threads[i]);
  }

  free (threads);
 }

 return NULL;
}

void event_listen (enum einit_event_subsystems type, void (* handler)(struct einit_event *)) {
 struct event_function *fstruct = ecalloc (1, sizeof (struct event_function));

 fstruct->type = type & EVENT_SUBSYSTEM_MASK;
 fstruct->handler = handler;

 emutex_lock (&evf_mutex);
  if (event_functions)
   fstruct->next = event_functions;

  event_functions = fstruct;
 emutex_unlock (&evf_mutex);
}

void event_ignore (enum einit_event_subsystems type, void (* handler)(struct einit_event *)) {
 if (!event_functions) return;

 uint32_t ltype = type & EVENT_SUBSYSTEM_MASK;

 emutex_lock (&evf_mutex);
  struct event_function *cur = event_functions;
  struct event_function *prev = NULL;
  while (cur) {
   if ((cur->type==ltype) && (cur->handler==handler)) {
    if (prev == NULL) {
     event_functions = cur->next;
     free (cur);
     cur = event_functions;
    } else {
     prev->next = cur->next;
     free (cur);
     cur = prev->next;
    }
   } else {
    prev = cur;
    cur = cur->next;
   }
  }
 emutex_unlock (&evf_mutex);

 return;
}

void function_register_type (const char *name, uint32_t version, void const *function, enum function_type type, struct lmodule *module) {
 if (!name || !function) return;

 char added = 0;

 if (module) {
  emutex_lock (&pof_mutex);
  struct stree *ha = exported_functions;
  ha = streefind (exported_functions, name, tree_find_first);
  while (ha) {
   struct exported_function *ef = ha->value;
   if (ef && (ef->version == version) && (ef->type == type) && (ef->module == module)) {
    ef->function = function;
	added = 1;
	break;
   }

   ha = streefind (ha, name, tree_find_next);
  }
  emutex_unlock (&pof_mutex);
 }


 if (!added) {
  struct exported_function *fstruct = ecalloc (1, sizeof (struct exported_function));

  fstruct->type     = type;
  fstruct->version  = version;
  fstruct->function = function;
  fstruct->module   = module;

  emutex_lock (&pof_mutex);
   exported_functions = streeadd (exported_functions, name, (void *)fstruct, sizeof(struct exported_function), NULL);
  emutex_unlock (&pof_mutex);

  free (fstruct);
 }
}

void function_unregister_type (const char *name, uint32_t version, void const *function, enum function_type type, struct lmodule *module) {
 if (!exported_functions) return;
 struct stree *ha = exported_functions;

 emutex_lock (&pof_mutex);
 ha = streefind (exported_functions, name, tree_find_first);
 while (ha) {
  struct exported_function *ef = ha->value;
  if (ef && (ef->version == version) && (ef->type == type)) {
   exported_functions = streedel (ha);
   ha = streefind (exported_functions, name, tree_find_first);
  } else
   ha = streefind (ha, name, tree_find_next);
 }
 emutex_unlock (&pof_mutex);

 return;
}

void **function_find (const char *name, const uint32_t version, const char ** sub) {
 if (!exported_functions || !name) return NULL;
 void **set = NULL;
 struct stree *ha = exported_functions;

 emutex_lock (&pof_mutex);
 if (!sub) {
  ha = streefind (exported_functions, name, tree_find_first);
  while (ha) {
   struct exported_function *ef = ha->value;
   if (ef && (ef->version == version)) set = setadd (set, (void*)ef->function, -1);
   ha = streefind (ha, name, tree_find_next);
  }
 } else {
  uint32_t i = 0, k = strlen (name)+1;
  char *n = emalloc (k+1);
  *n = 0;
  strcat (n, name);
  *(n + k - 1) = '-';

  for (; sub[i]; i++) {
   *(n + k) = 0;
   n = erealloc (n, k+1+strlen (sub[i]));
   strcat (n, sub[i]);

   ha = streefind (exported_functions, n, tree_find_first);

   while (ha) {

    struct exported_function *ef = ha->value;
    if (ef && (ef->version == version)) set = setadd (set, (void*)ef->function, -1);

    ha = streefind (ha, n, tree_find_next);
   }
  }

  if (n) free (n);
 }
 emutex_unlock (&pof_mutex);

 return set;
}

void *function_find_one (const char *name, const uint32_t version, const char ** sub) {
 void **t = function_find(name, version, sub);
 void *f = (t? t[0] : NULL);

 if (t) free (t);

 return f;
}

struct exported_function **function_look_up (const char *name, const uint32_t version, const char **sub) {
 if (!exported_functions || !name) return NULL;
 struct exported_function **set = NULL;
 struct stree *ha = exported_functions;

 emutex_lock (&pof_mutex);
 if (!sub) {
  ha = streefind (exported_functions, name, tree_find_first);
  while (ha) {
   struct exported_function *ef = ha->value;

   if (!(ef->name)) ef->name = ha->key;

   if (ef && (ef->version == version)) set = (struct exported_function **)setadd ((void **)set, (struct exported_function *)ef, -1);
   ha = streefind (ha, name, tree_find_next);
  }
 } else {
  uint32_t i = 0, k = strlen (name)+1;
  char *n = emalloc (k+1);
  *n = 0;
  strcat (n, name);
  *(n + k - 1) = '-';

  for (; sub[i]; i++) {
   *(n + k) = 0;
   n = erealloc (n, k+1+strlen (sub[i]));
   strcat (n, sub[i]);

   ha = streefind (exported_functions, n, tree_find_first);

   while (ha) {
    struct exported_function *ef = ha->value;

    if (!(ef->name)) ef->name = ha->key;

    if (ef && (ef->version == version)) set = (struct exported_function **)setadd ((void **)set, (struct exported_function *)ef, -1);

    ha = streefind (ha, n, tree_find_next);
   }
  }

  if (n) free (n);
 }
 emutex_unlock (&pof_mutex);

 return set;
}

struct exported_function *function_look_up_one (const char *name, const uint32_t version, const char **sub) {
 struct exported_function **t = function_look_up(name, version, sub);
 struct exported_function *f = (t? t[0] : NULL);

 if (t) free (t);

 return f;
}

char *event_code_to_string (const uint32_t code) {
 switch (code) {
  case einit_core_update_configuration:   return "core/update-configuration";
  case einit_core_module_update:          return "core/module-status-update";
  case einit_core_service_update:         return "core/service-status-update";
  case einit_core_configuration_update:   return "core/configuration-status-update";

  case einit_mount_do_update:              return "mount/update";

  case einit_feedback_module_status: return "feedback/module";
  case einit_feedback_plan_status:   return "feedback/plan";
  case einit_feedback_notice:        return "feedback/notice";
 }

 switch (code & EVENT_SUBSYSTEM_MASK) {
  case einit_event_subsystem_core:    return "core/{unknown}";
  case einit_event_subsystem_ipc:      return "ipc/{unknown}";
  case einit_event_subsystem_mount:    return "mount/{unknown}";
  case einit_event_subsystem_feedback: return "feedback/{unknown}";
  case einit_event_subsystem_power:    return "power/{unknown}";
  case einit_event_subsystem_timer:    return "timer/{unknown}";
  case einit_event_subsystem_custom:   return "custom/{unknown}";
 }

 return "unknown/custom";
}

uint32_t event_string_to_code (const char *code) {
 char **tcode = str2set ('/', code);
 uint32_t ret = einit_event_subsystem_custom;

 if (tcode) {
  if (strmatch (tcode[0], "core"))          ret = einit_event_subsystem_core;
  else if (strmatch (tcode[0], "ipc"))      ret = einit_event_subsystem_ipc;
  else if (strmatch (tcode[0], "mount"))    ret = einit_event_subsystem_mount;
  else if (strmatch (tcode[0], "feedback")) ret = einit_event_subsystem_feedback;
  else if (strmatch (tcode[0], "power"))    ret = einit_event_subsystem_power;
  else if (strmatch (tcode[0], "timer"))    ret = einit_event_subsystem_timer;

  if (tcode[1])
   switch (ret) {
    case einit_event_subsystem_core:
     if (strmatch (tcode[1], "update-configuration")) ret = einit_core_update_configuration;
     else if (strmatch (tcode[1], "module-status-update")) ret = einit_core_module_update;
     else if (strmatch (tcode[1], "service-status-update")) ret = einit_core_service_update;
     else if (strmatch (tcode[1], "configuration-status-update")) ret = einit_core_configuration_update;
     break;
    case einit_event_subsystem_mount:
     if (strmatch (tcode[1], "update")) ret = einit_mount_do_update;
     break;
    case einit_event_subsystem_feedback:
     if (strmatch (tcode[1], "module")) ret = einit_feedback_module_status;
     else if (strmatch (tcode[1], "plan")) ret = einit_feedback_plan_status;
     else if (strmatch (tcode[1], "notice")) ret = einit_feedback_notice;
     break;
    case einit_event_subsystem_power:
     if (strmatch (tcode[1], "reset-scheduled")) ret = einit_power_reset_scheduled;
     else if (strmatch (tcode[1], "reset-imminent")) ret = einit_power_reset_imminent;
     else if (strmatch (tcode[1], "mps-down-scheduled")) ret = einit_power_down_scheduled;
     else if (strmatch (tcode[1], "mps-down-imminent")) ret = einit_power_down_imminent;
     break;
   }

  free (tcode);
 }

 return ret;
}

time_t event_timer_register (struct tm *t) {
 struct einit_event ev = evstaticinit (einit_timer_set);
 time_t tr = timegm (t);

 ev.integer = tr;

 event_emit (&ev, einit_event_flag_broadcast);

 evstaticdestroy (ev);

 return tr;
}

time_t event_timer_register_timeout (time_t t) {
 struct einit_event ev = evstaticinit (einit_timer_set);
 time_t tr = time (NULL) + t;

 ev.integer = tr;

 event_emit (&ev, einit_event_flag_broadcast);

 evstaticdestroy (ev);

 return tr;
}

void event_timer_cancel (time_t t) {
 struct einit_event ev = evstaticinit (einit_timer_cancel);

 ev.integer = t;

 event_emit (&ev, einit_event_flag_broadcast);

 evstaticdestroy (ev);
}

void event_wakeup (enum einit_event_code c, struct lmodule *m) {
 struct wakeup_data *d = event_wd;

 while (d) {
  if (d->code == c) {
   if (d->module == m) {
    return;
   } else {
    emutex_lock (&event_wakeup_mutex);
    if (d->module == NULL) {
     d->module = m;
    }
    emutex_unlock (&event_wakeup_mutex);

    if (d->module == m)
	 return;
   }
  }
  d = d->next;
 }

 struct wakeup_data *nd = ecalloc (1, sizeof (struct wakeup_data));

 nd->code = c;
 nd->module = m;

 emutex_lock (&event_wakeup_mutex);
 nd->next = event_wd;
 event_wd = nd;
 emutex_unlock (&event_wakeup_mutex);
}

void event_wakeup_cancel (enum einit_event_code c, struct lmodule *m) {
 struct wakeup_data *d = event_wd;

 while (d) {
  if (d->code == c) {
   emutex_lock (&event_wakeup_mutex);
   if (d->module == m)
    d->module = NULL;
   emutex_unlock (&event_wakeup_mutex);
  }
  d = d->next;
 }
}
