/*
 *  ipc-core-helpers.c
 *  einit
 *
 *  Created by Magnus Deininger on 19/03/2007.
 *  Copyright 2006 Magnus Deininger. All rights reserved.
 *
 */

/*
Copyright (c) 2006, Magnus Deininger
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

#define _MODULE

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <einit/module.h>
#include <einit/config.h>
#include <einit/bitch.h>
#include <errno.h>
#include <string.h>

#define EXPECTED_EIV 1

#if EXPECTED_EIV != EINIT_VERSION
#warning "This module was developed for a different version of eINIT, you might experience problems"
#endif

int _einit_ipc_core_helpers_configure (struct lmodule *);

#if defined(_EINIT_MODULE) || defined(_EINIT_MODULE_HEADER)

const struct smodule _einit_ipc_core_helpers_self = {
 .eiversion = EINIT_VERSION,
 .eibuild   = BUILDNUMBER,
 .version   = 1,
 .mode      = 0,
 .options   = 0,
 .name      = "IPC Command Library: Core Helpers",
 .rid       = "ipc-core-helpers",
 .si        = {
  .provides = NULL,
  .requires = NULL,
  .after    = NULL,
  .before   = NULL
 },
 .configure = _einit_ipc_core_helpers_configure
};

module_register(_einit_ipc_core_helpers_self);

#endif

struct lmodule *mlist;

void _einit_ipc_core_helpers_ipc_event_handler (struct einit_event *);

#define STATUS2STRING(status)\
 (status == STATUS_IDLE ? "idle" : \
 (status & STATUS_WORKING ? "working" : \
 (status & STATUS_ENABLED ? "enabled" : "disabled")))
#define STATUS2STRING_SHORT(status)\
 (status == STATUS_IDLE ? "I" : \
 (status & STATUS_WORKING ? "W" : \
 (status & STATUS_ENABLED ? "E" : "D")))

void _einit_ipc_core_helpers_ipc_event_handler (struct einit_event *ev) {
 if (!ev || !ev->set) return;
 char **argv = (char **) ev->set;
 int argc = setcount ((const void **)ev->set);
 uint32_t options = ev->status;

 if (argc >= 2) {
  if (strmatch (argv[0], "list")) {
   if (strmatch (argv[1], "modules")) {
    struct lmodule *cur = mlist;

    if (!ev->flag) ev->flag = 1;

    while (cur) {
     if ((cur->module && !(options & EIPC_ONLY_RELEVANT)) || (cur->status != STATUS_IDLE)) {
      if (options & EIPC_OUTPUT_XML) {
       eprintf ((FILE *)ev->para, " <module id=\"%s\" name=\"%s\"\n  status=\"%s\"",
                 (cur->module->rid ? cur->module->rid : "unknown"), (cur->module->name ? cur->module->name : "unknown"), STATUS2STRING(cur->status));
      } else {
       eprintf ((FILE *)ev->para, "[%s] %s (%s)",
                 STATUS2STRING_SHORT(cur->status), (cur->module->rid ? cur->module->rid : "unknown"), (cur->module->name ? cur->module->name : "unknown"));
      }

      if (cur->si) {
       if (cur->si->provides) {
        if (options & EIPC_OUTPUT_XML) {
         eprintf ((FILE *)ev->para, "\n  provides=\"%s\"", set2str(':', (const char **)cur->si->provides));
        } else {
         eprintf ((FILE *)ev->para, "\n > provides: %s", set2str(' ', (const char **)cur->si->provides));
        }
       }
       if (cur->si->requires) {
        if (options & EIPC_OUTPUT_XML) {
         eprintf ((FILE *)ev->para, "\n  requires=\"%s\"", set2str(':', (const char **)cur->si->requires));
        } else {
         eprintf ((FILE *)ev->para, "\n > requires: %s", set2str(' ', (const char **)cur->si->requires));
        }
       }
       if (cur->si->after) {
        if (options & EIPC_OUTPUT_XML) {
         eprintf ((FILE *)ev->para, "\n  after=\"%s\"", set2str(':', (const char **)cur->si->after));
        } else {
         eprintf ((FILE *)ev->para, "\n > after: %s", set2str(' ', (const char **)cur->si->after));
        }
       }
       if (cur->si->before) {
        if (options & EIPC_OUTPUT_XML) {
         eprintf ((FILE *)ev->para, "\n  before=\"%s\"", set2str(':', (const char **)cur->si->before));
        } else {
         eprintf ((FILE *)ev->para, "\n > before: %s", set2str(' ', (const char **)cur->si->before));
        }
       }
      }

      if (options & EIPC_OUTPUT_XML) {
       eputs (" />\n", (FILE *)ev->para);
      } else {
       eputs ("\n", (FILE *)ev->para);
      }
     }
     cur = cur->next;
    }
   }
  }
 }
}


int _einit_ipc_core_helpers_cleanup (struct lmodule *irr) {
 event_ignore (EVENT_SUBSYSTEM_IPC, _einit_ipc_core_helpers_ipc_event_handler);

 return 0;
}

int _einit_ipc_core_helpers_configure (struct lmodule *r) {
 module_init (r);

 thismodule->cleanup = _einit_ipc_core_helpers_cleanup;

 event_listen (EVENT_SUBSYSTEM_IPC, _einit_ipc_core_helpers_ipc_event_handler);

 return 0;
}

/* passive module, no enable/disable/etc */