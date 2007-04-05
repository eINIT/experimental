/*
 *  module-so.c
 *  einit
 *
 *  split from module.c on 19/03/2007.
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

#define _MODULE

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <einit/module.h>
#include <einit/config.h>
#include <einit/bitch.h>
#include <einit/utility.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <einit-modules/configuration.h>
#include <einit/configuration.h>

#define EXPECTED_EIV 1

#if EXPECTED_EIV != EINIT_VERSION
#warning "This module was developed for a different version of eINIT, you might experience problems"
#endif

int _einit_mod_so_configure (struct lmodule *);

#if defined(_EINIT_MODULE) || defined(_EINIT_MODULE_HEADER)

const struct smodule _einit_mod_so_self = {
 .eiversion = EINIT_VERSION,
 .eibuild   = BUILDNUMBER,
 .version   = 1,
 .mode      = EINIT_MOD_LOADER,
 .options   = 0,
 .name      = "Module Support (.so)",
 .rid       = "module-so",
 .si        = {
  .provides = NULL,
  .requires = NULL,
  .after    = NULL,
  .before   = NULL
 },
 .configure = _einit_mod_so_configure
};

module_register(_einit_mod_so_self);

#endif

pthread_mutex_t modules_update_mutex = PTHREAD_MUTEX_INITIALIZER;

int _einit_mod_so_scanmodules (struct lmodule *);

int _einit_mod_so_cleanup (struct lmodule *pa) {
 return 0;
}

int _einit_mod_so_scanmodules ( struct lmodule *modchain ) {
 void *sohandle;
 char **modules = NULL;

 emutex_lock (&modules_update_mutex);

 modules = readdirfilter(cfg_getnode ("core-settings-modules", NULL),
#ifdef DO_BOOTSTRAP
                                bootstrapmodulepath
#else
                                "/lib/einit/modules/"
#endif
                                , ".*\\.so", NULL, 0);

 if (modules) {
  uint32_t z = 0;

  for (; modules[z]; z++) {
   struct smodule **modinfo;
   struct lmodule *lm = modchain;

   while (lm) {
    if (lm->source && strmatch(lm->source, modules[z])) {
     lm = mod_update (lm);

     if (lm->module && (lm->module->mode & EINIT_MOD_LOADER) && (lm->scanmodules != NULL)) {
      lm->scanmodules (modchain);
     }

     goto cleanup_continue;
    }
    lm = lm->next;
   }

   dlerror();

   sohandle = dlopen (modules[z], RTLD_NOW);
   if (sohandle == NULL) {
    eputs (dlerror (), stdout);
    goto cleanup_continue;
   }

   modinfo = (struct smodule **)dlsym (sohandle, "self");
   if ((modinfo != NULL) && ((*modinfo) != NULL)) {
    if ((*modinfo)->eibuild == BUILDNUMBER) {
     struct lmodule *new = mod_add (sohandle, (*modinfo));
     if (new) {
      new->source = estrdup(modules[z]);
     }
    } else {
     notice (1, "module %s: not loading: different build number: %i.\n", modules[z], (*modinfo)->eibuild);

     dlclose (sohandle);
    }
   } else {
    notice (1, "module %s: not loading: missing header.\n", modules[z]);

    dlclose (sohandle);
   }

   cleanup_continue: ;
  }

  free (modules);
 }


 emutex_unlock (&modules_update_mutex);

 return 1;
}

int _einit_mod_so_configure (struct lmodule *pa) {
 module_init (pa);

 pa->scanmodules = _einit_mod_so_scanmodules;
 pa->cleanup = _einit_mod_so_cleanup;

 return 0;
}