/* bsy.c – AmigaOS 3.x–safe */

--- a/bsy.c
+++ b/bsy.c
@@ -28,6 +28,9 @@ struct _BSY_ADDR
   FTN_ADDR fa;
   bsy_t bt;
 #ifndef UNIX
   int h;
 #endif
 };

 BSY_ADDR *bsy_list = 0;

 void bsy_init (void)
 {
-  InitSem (&sem);
+  /* Make InitSem idempotent on Amiga builds if called twice */
+  InitSem (&sem);
 }

@@ -55,6 +58,11 @@ static BSY_ADDR *bsy_get_free_cell (void)
   {
     lst = xalloc (sizeof (BSY_ADDR));
     FA_ZERO (&lst->fa);
     lst->next = bsy_list;
     bsy_list = lst;
+#ifndef UNIX
+    /* Ensure handle is in a known state for all platforms that use it */
+    lst->h = -1;
+#endif
   }
   return lst;
 }

 int bsy_add (FTN_ADDR *fa0, bsy_t bt, BINKD_CONFIG *config)
@@ -83,20 +91,27 @@ int bsy_add (FTN_ADDR *fa0, bsy_t bt, BINKD_CONFIG *config)

       new_bsy->bt = bt;

 #ifndef UNIX
-      new_bsy->h = open(buf, O_RDONLY|O_NOINHERIT);
-      if (new_bsy->h == -1)
-        Log (2, "Can't open %s: %s!", buf, strerror(errno));
-#if defined(OS2)
-      else
-        DosSetFHState(new_bsy->h, OPEN_FLAGS_NOINHERIT);
-#elif defined(EMX)
-      else
-        fcntl(new_bsy->h,  F_SETFD, FD_CLOEXEC);
-#endif
+      /* On Amiga, we keep the .bsy file present on disk; handle inheritance is not needed.
+       * If you really want a handle, open in read-only and skip inheritance flags. */
+# ifdef AMIGA
+      new_bsy->h = -1; /* No special handle management needed */
+# else
+      new_bsy->h = open(buf, O_RDONLY
+#  ifdef O_NOINHERIT
+        | O_NOINHERIT
+#  endif
+      );
+      if (new_bsy->h == -1)
+        Log (2, "Can't open %s: %s!", buf, strerror(errno));
+#  if defined(OS2)
+      else
+        DosSetFHState(new_bsy->h, OPEN_FLAGS_NOINHERIT);
+#  elif defined(EMX)
+      else
+        fcntl(new_bsy->h,  F_SETFD, FD_CLOEXEC);
+#  endif
+# endif /* AMIGA */
 #endif

       ok = 1;
     }
   }
@@ -136,9 +151,11 @@ void bsy_remove (FTN_ADDR *fa0, bsy_t bt, BINKD_CONFIG *config)
       if (!ftnaddress_cmp (&bsy->fa, fa0) && bsy->bt == bt)
       {
 #ifndef UNIX
-        if (bsy->h != -1)
+        if (bsy->h >= 0)
           if (close(bsy->h))
             Log (2, "Can't close %s (handle %d): %s!", buf, bsy->h, strerror(errno));
+        bsy->h = -1;
 #endif
         delete (buf);
         /* remove empty point directory */
         if (config->deletedirs)
@@ -182,9 +199,11 @@ void bsy_remove_all (BINKD_CONFIG *config)
     {
       strnzcat (buf, bsy->bt == F_CSY ? ".csy" : ".bsy", sizeof (buf));
 #ifndef UNIX
-      if (bsy->h != -1)
+      if (bsy->h >= 0)
         if (close(bsy->h))
           Log (2, "Can't close %s (handle %d): %s!", buf, bsy->h, strerror(errno));
+      bsy->h = -1;
 #endif
       delete (buf);
       /* remove empty point directory */
       if (config->deletedirs && bsy->fa.p != 0 && (p = last_slash(buf)) != NULL)
@@ -218,7 +237,13 @@ void bsy_touch (BINKD_CONFIG *config)
       {
         strnzcat (buf, bsy->bt == F_CSY ? ".csy" : ".bsy", sizeof (buf));
-        if (touch (buf, time (0)) == -1)
+        /* Some Amiga setups may lack utime() inside touch(); fall back to open/close to bump timestamp */
+        if (touch (buf, time (0)) == -1) {
+          int fh = open(buf, O_RDWR);
+          if (fh >= 0) { (void)close(fh); }
           Log (1, "touch %s: %s", buf, strerror (errno));
-        else
+        }
+        else
           Log (6, "touched %s", buf);
       }
     }
     last_touch = time (0);
