/*
** Copyright (c) 2017 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file implements various web pages use for running a security audit
** of a Fossil configuration.
*/
#include "config.h"
#include <assert.h>
#include "security_audit.h"

/*
** Return TRUE if any of the capability letters in zTest are found
** in the capability string zCap.
*/
static int hasAnyCap(const char *zCap, const char *zTest){
  while( zTest[0] ){
    if( strchr(zCap, zTest[0]) ) return 1;
    zTest++;
  }
  return 0;
}


/*
** WEBPAGE: secaudit0
**
** Run a security audit of the current Fossil setup.
** This page requires administrator access
*/
void secaudit0_page(void){
  const char *zAnonCap;      /* Capabilities of user "anonymous" and "nobody" */
  const char *zPubPages;     /* GLOB pattern for public pages */
  const char *zSelfCap;      /* Capabilities of self-registered users */
  char *z;
  int n;

  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed(0);
    return;
  }
  style_header("Security Audit");
  @ <ol>

  /* Step 1:  Determine if the repository is public or private.  "Public"
  ** means that any anonymous user on the internet can access all content.
  ** "Private" repos require (non-anonymous) login to access all content,
  ** though some content may be accessible anonymously.
  */
  zAnonCap = db_text("", "SELECT fullcap(NULL)");
  zPubPages = db_get("public-pages",0);
  if( db_get_boolean("self-register",0) ){
    CapabilityString *pCap;
    pCap = capability_add(0, db_get("default-perms",""));
    capability_expand(pCap);
    zSelfCap = capability_string(pCap);
    capability_free(pCap);
  }else{
    zSelfCap = fossil_strdup("");
  }
  if( hasAnyCap(zAnonCap,"as") ){
    @ <li><p>This repository is <big><b>Wildly INSECURE</b></big> because
    @ it grants administrator privileges to anonymous users.  You
    @ should <a href="takeitprivate">take this repository private</a>
    @ immediately!  Or, at least remove the Setup and Admin privileges
    @ for users "anonymous" and "login" on the
    @ <a href="setup_ulist">User Configuration</a> page.
  }else if( hasAnyCap(zSelfCap,"as") ){
    @ <li><p>This repository is <big><b>Wildly INSECURE</b></big> because
    @ it grants administrator privileges to self-registered users.  You
    @ should <a href="takeitprivate">take this repository private</a>
    @ and/or disable self-registration
    @ immediately!  Or, at least remove the Setup and Admin privileges
    @ from the default permissions for new users.
  }else if( hasAnyCap(zAnonCap,"y") ){
    @ <li><p>This repository is <big><b>INSECURE</b></big> because
    @ it allows anonymous users to push unversioned files.
    @ <p>Fix this by <a href="takeitprivate">taking the repository private</a>
    @ or by removing the "y" permission from users "anonymous" and
    @ "nobody" on the <a href="setup_ulist">User Configuration</a> page.
  }else if( hasAnyCap(zSelfCap,"y") ){
    @ <li><p>This repository is <big><b>INSECURE</b></big> because
    @ it allows self-registered users to push unversioned files.
    @ <p>Fix this by <a href="takeitprivate">taking the repository private</a>
    @ or by removing the "y" permission from the default permissions or
    @ by disabling self-registration.
  }else if( hasAnyCap(zAnonCap,"goz") ){
    @ <li><p>This repository is <big><b>PUBLIC</b></big>. All
    @ checked-in content can be accessed by anonymous users.
    @ <a href="takeitprivate">Take it private</a>.<p>
  }else if( hasAnyCap(zSelfCap,"goz") ){
    @ <li><p>This repository is <big><b>PUBLIC</b></big> because all
    @ checked-in content can be accessed by self-registered users.
    @ This repostory would be private if you disabled self-registration.</p>
  }else if( !hasAnyCap(zAnonCap, "jrwy234567")
         && !hasAnyCap(zSelfCap, "jrwy234567")
         && (zPubPages==0 || zPubPages[0]==0) ){
    @ <li><p>This repository is <big><b>Completely PRIVATE</b></big>.
    @ A valid login and password is required to access any content.
  }else{
    @ <li><p>This repository is <big><b>Mostly PRIVATE</b></big>.
    @ A valid login and password is usually required, however some
    @ content can be accessed either anonymously or by self-registered
    @ users:
    @ <ul>
    if( hasAnyCap(zAnonCap,"j") || hasAnyCap(zSelfCap,"j") ){
      @ <li> Wiki pages
    }
    if( hasAnyCap(zAnonCap,"r") || hasAnyCap(zSelfCap,"r") ){
      @ <li> Tickets
    }
    if( hasAnyCap(zAnonCap,"234567") || hasAnyCap(zSelfCap,"234567") ){
      @ <li> Forum posts
    }
    if( zPubPages && zPubPages[0] ){
      Glob *pGlob = glob_create(zPubPages);
      int i;
      @ <li> URLs that match any of these GLOB patterns:
      @ <ul>
      for(i=0; i<pGlob->nPattern; i++){
        @ <li> %h(pGlob->azPattern[i])
      }
      @ </ul>
    }
    @ </ul>
    if( zPubPages && zPubPages[0] ){
      @ <p>Change GLOB patterns exceptions using the "Public pages" setting
      @ on the <a href="setup_access">Access Settings</a> page.</p>
    }
  }

  /* Make sure the HTTPS is required for login, at least, so that the
  ** password does not go across the Internet in the clear.
  */
  if( db_get_int("redirect-to-https",0)==0 ){
    @ <li><p><b>WARNING:</b>
    @ Sensitive material such as login passwords can be sent over an
    @ unencrypted connection.
    @ <p>Fix this by changing the "Redirect to HTTPS" setting on the
    @ <a href="setup_access">Access Control</a> page. If you were using
    @ the old "Redirect to HTTPS on Login Page" setting, switch to the
    @ new setting: it has a more secure implementation.
  }

  /* Anonymous users should not be able to harvest email addresses
  ** from tickets.
  */
  if( hasAnyCap(zAnonCap, "e") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can view email addresses and other personally
    @ identifiable information on tickets.
    @ <p>Fix this by removing the "Email" privilege
    @ (<a href="setup_ucap_list">capability "e"</a>) from users
    @ "anonymous" and "nobody" on the
    @ <a href="setup_ulist">User Configuration</a> page.
  }

  /* Anonymous users probably should not be allowed to push content
  ** to the repository.
  */
  if( hasAnyCap(zAnonCap, "i") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can push new check-ins into the repository.
    @ <p>Fix this by removing the "Check-in" privilege
    @ (<a href="setup_ucap_list">capability</a> "i") from users
    @ "anonymous" and "nobody" on the
    @ <a href="setup_ulist">User Configuration</a> page.
  }

  /* Anonymous users probably should not be allowed act as moderators
  ** for wiki or tickets.
  */
  if( hasAnyCap(zAnonCap, "lq5") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can act as moderators for wiki, tickets, or 
    @ forum posts. This defeats the whole purpose of moderation.
    @ <p>Fix this by removing the "Mod-Wiki", "Mod-Tkt", and "Mod-Forum"
    @ privileges (<a href="%R/setup_ucap_list">capabilities</a> "fq5")
    @ from users "anonymous" and "nobody"
    @ on the <a href="setup_ulist">User Configuration</a> page.
  }

  /* Anonymous users probably should not be allowed to delete
  ** wiki or tickets.
  */
  if( hasAnyCap(zAnonCap, "d") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can delete wiki and tickets.
    @ <p>Fix this by removing the "Delete"
    @ privilege from users "anonymous" and "nobody" on the
    @ <a href="setup_ulist">User Configuration</a> page.
  }

  /* If anonymous users are allowed to create new Wiki, then
  ** wiki moderation should be activated to pervent spam.
  */
  if( hasAnyCap(zAnonCap, "fk") ){
    if( db_get_boolean("modreq-wiki",0)==0 ){
      @ <li><p><b>WARNING:</b>
      @ Anonymous users can create or edit wiki without moderation.
      @ This can result in robots inserting lots of wiki spam into
      @ repository.
      @ Fix this by removing the "New-Wiki" and "Write-Wiki"
      @ privileges from users "anonymous" and "nobody" on the
      @ <a href="setup_ulist">User Configuration</a> page or
      @ by enabling wiki moderation on the
      @ <a href="setup_modreq">Moderation Setup</a> page.
    }else{
      @ <li><p>
      @ Anonymous users can create or edit wiki, but moderator
      @ approval is required before the edits become permanent.
    }
  }

  /* Anonymous users should not be able to create trusted forum
  ** posts.
  */
  if( hasAnyCap(zAnonCap, "456") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can create forum posts that are
    @ accepted into the permanent record without moderation.
    @ This can result in robots generating spam on forum posts.
    @ Fix this by removing the "WriteTrusted-Forum" privilege
    @ (<a href="setup_ucap_list">capabilities</a> "456") from
    @ users "anonymous" and "nobody" on the
    @ <a href="setup_ulist">User Configuration</a> page or
  }

  /* Anonymous users should not be able to send announcements.
  */
  if( hasAnyCap(zAnonCap, "A") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can send announcements to anybody who is signed
    @ up to receive announcements.  This can result in spam.
    @ Fix this by removing the "Announce" privilege
    @ (<a href="setup_ucap_list">capability</a> "A") from
    @ users "anonymous" and "nobody" on the
    @ <a href="setup_ulist">User Configuration</a> page or
  }

  /* Administrative privilege should only be provided to
  ** specific individuals, not to entire classes of people.
  ** And not too many people should have administrator privilege.
  */
  z = db_text(0,
    "SELECT group_concat("
                 "printf('<a href=''setup_uedit?id=%%d''>%%s</a>',uid,login),"
             "' and ')"
    " FROM user"
    " WHERE cap GLOB '*[as]*'"
    "   AND login in ('anonymous','nobody','reader','developer')"
  );
  if( z && z[0] ){
    @ <li><p><b>WARNING:</b>
    @ Administrative privilege ('a' or 's')
    @ is granted to an entire class of users: %s(z).
    @ Administrative privilege should only be
    @ granted to specific individuals.
  }
  n = db_int(0,"SELECT count(*) FROM user WHERE fullcap(cap) GLOB '*[as]*'");
  if( n==0 ){
    @ <li><p>
    @ No users have administrator privilege.
  }else{
    z = db_text(0,
      "SELECT group_concat("
                 "printf('<a href=''setup_uedit?id=%%d''>%%s</a>',uid,login),"
             "', ')"
      " FROM user"
      " WHERE fullcap(cap) GLOB '*[as]*'"
    );
    @ <li><p>
    @ Users with administrator privilege are: %s(z)
    fossil_free(z);
    if( n>3 ){
      @ <li><p><b>WARNING:</b>
      @ Administrator privilege is granted to
      @ <a href='setup_ulist?with=as'>%d(n) users</a>.
      @ Ideally, administator privilege ('s' or 'a') should only
      @ be granted to one or two users.
    }
  }

  /* The push-unversioned privilege should only be provided to
  ** specific individuals, not to entire classes of people.
  ** And no too many people should have this privilege.
  */
  z = db_text(0,
    "SELECT group_concat("
                 "printf('<a href=''setup_uedit?id=%%d''>%%s</a>',uid,login),"
             "' and ')"
    " FROM user"
    " WHERE cap GLOB '*y*'"
    "   AND login in ('anonymous','nobody','reader','developer')"
  );
  if( z && z[0] ){
    @ <li><p><b>WARNING:</b>
    @ The "Write-Unver" privilege is granted to an entire class of users: %s(z).
    @ The Write-Unver privilege should only be granted to specific individuals.
    fossil_free(z);
  }
  n = db_int(0,"SELECT count(*) FROM user WHERE cap GLOB '*y*'");
  if( n>0 ){
    z = db_text(0,
       "SELECT group_concat("
          "printf('<a href=''setup_uedit?id=%%d''>%%s</a>',uid,login),', ')"
       " FROM user WHERE fullcap(cap) GLOB '*y*'"
    );
    @ <li><p>
    @ Users with "Write-Unver" privilege: %s(z)
    fossil_free(z);
    if( n>3 ){
      @ <p><b>Caution:</b>
      @ The "Write-Unver" privilege ('y') is granted to an excessive
      @ number of users (%d(n)).
      @ Ideally, the Write-Unver privilege should only
      @ be granted to one or two users.
    }
  }

  /* Notify if REMOTE_USER or HTTP_AUTHENTICATION is used for login.
  */
  if( db_get_boolean("remote_user_ok", 0) ){
    @ <li><p>
    @ This repository trusts that the REMOTE_USER environment variable set
    @ up by the webserver contains the name of an authenticated user.
    @ Fossil's built-in authentication mechanism is bypassed.
    @ <p>Fix this by deactivating the "Allow REMOTE_USER authentication"
    @ checkbox on the <a href="setup_access">Access Control</a> page.
  }
  if( db_get_boolean("http_authentication_ok", 0) ){
    @ <li><p>
    @ This repository trusts that the HTTP_AUTHENITICATION environment
    @ variable set up by the webserver contains the name of an
    @ authenticated user.
    @ Fossil's built-in authentication mechanism is bypassed.
    @ <p>Fix this by deactivating the "Allow HTTP_AUTHENTICATION authentication"
    @ checkbox on the <a href="setup_access">Access Control</a> page.
  }

  /* Logging should be turned on
  */
  if( db_get_boolean("access-log",0)==0 ){
    @ <li><p>
    @ The <a href="access_log">User Log</a> is disabled.  The user log
    @ keeps a record of successful and unsucessful login attempts and is
    @ useful for security monitoring.
  }
  if( db_get_boolean("admin-log",0)==0 ){
    @ <li><p>
    @ The <a href="admin_log">Administrative Log</a> is disabled.
    @ The administrative log provides a record of configuration changes
    @ and is useful for security monitoring.
  }

#if !defined(_WIN32) && !defined(FOSSIL_OMIT_LOAD_AVERAGE)
  /* Make sure that the load-average limiter is armed and working */
  if( load_average()==0.0 ){
    @ <li><p>
    @ Unable to get the system load average.  This can prevent Fossil
    @ from throttling expensive operations during peak demand.
    @ <p>If running in a chroot jail on Linux, verify that the /proc
    @ filesystem is mounted within the jail, so that the load average
    @ can be obtained from the /proc/loadavg file.
  }else {
    double r = atof(db_get("max-loadavg", "0"));
    if( r<=0.0 ){
      @ <li><p>
      @ Load average limiting is turned off.  This can cause the server
      @ to bog down if many requests for expensive services (such as
      @ large diffs or tarballs) arrive at about the same time.
      @ <p>To fix this, set the "Server Load Average Limit" on the
      @ <a href="setup_access">Access Control</a> page to approximately
      @ the number of available cores on your server, or maybe just a little
      @ less.
    }else if( r>=8.0 ){
      @ <li><p>
      @ The "Server Load Average Limit" on the
      @ <a href="setup_access">Access Control</a> page is set to %g(r),
      @ which seems high.  Is this server really a %d((int)r)-core machine?
    }
  }
#endif

  if( g.zErrlog==0 || fossil_strcmp(g.zErrlog,"-")==0 ){
    @ <li><p>
    @ The server error log is disabled.
    @ To set up an error log:
    @ <ul>
    @ <li>If running from CGI, make an entry "errorlog: <i>FILENAME</i>"
    @ in the CGI script.
    @ <li>If running the "fossil server" or "fossil http" commands,
    @ add the "--errorlog <i>FILENAME</i>" command-line option.
    @ </ul>
  }else{
    FILE *pTest = fossil_fopen(g.zErrlog,"a");
    if( pTest==0 ){
      @ <li><p>
      @ <b>Error:</b>
      @ There is an error log at "%h(g.zErrlog)" but that file is not
      @ writable and so no logging will occur.
    }else{
      fclose(pTest);
      @ <li><p>
      @ The error log at "<a href='%R/errorlog'>%h(g.zErrlog)</a>" that is
      @ %,lld(file_size(g.zErrlog, ExtFILE)) bytes in size.
    }
  }

  @ <li><p> User capability summary:
  capability_summary();

  if( alert_enabled() ){
    @ <li><p> Email alert configuration summary:
    @ <table class="label-value">
    stats_for_email();
    @ </table>
  }else{
    @ <li><p> Email alerts are disabled
  }

  @ </ol>
  style_footer();
}

/*
** WEBPAGE: takeitprivate
**
** Disable anonymous access to this website
*/
void takeitprivate_page(void){
  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed(0);
    return;
  }
  if( P("cancel") ){
    /* User pressed the cancel button.  Go back */
    cgi_redirect("secaudit0");
  }
  if( P("apply") ){
    db_multi_exec(
      "UPDATE user SET cap=''"
      " WHERE login IN ('nobody','anonymous');"
      "DELETE FROM config WHERE name='public-pages';"
    );
    db_set("self-register","0",0);
    cgi_redirect("secaudit0");
  }
  style_header("Make This Website Private");
  @ <p>Click the "Make It Private" button below to disable all
  @ anonymous access to this repository.  A valid login and password
  @ will be required to access this repository after clicking that
  @ button.</p>
  @
  @ <p>Click the "Cancel" button to leave things as they are.</p>
  @
  @ <form action="%s(g.zPath)" method="post">
  @ <input type="submit" name="apply" value="Make It Private">
  @ <input type="submit" name="cancel" value="Cancel">
  @ </form>

  style_footer();
}

/*
** The maximum number of bytes of log to show
*/
#define MXSHOWLOG 50000

/*
** WEBPAGE: errorlog
**
** Show the content of the error log.  Only the administrator can view
** this page.
*/
void errorlog_page(void){
  i64 szFile;
  FILE *in;
  char z[10000];
  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed(0);
    return;
  }
  style_header("Server Error Log");
  style_submenu_element("Test", "%R/test-warning");
  style_submenu_element("Refresh", "%R/errorlog");
  if( g.zErrlog==0 || fossil_strcmp(g.zErrlog,"-")==0 ){
    @ <p>To create a server error log:
    @ <ol>
    @ <li><p>
    @ If the server is running as CGI, then create a line in the CGI file
    @ like this:
    @ <blockquote><pre>
    @ errorlog: <i>FILENAME</i>
    @ </pre></blockquote>
    @ <li><p>
    @ If the server is running using one of 
    @ the "fossil http" or "fossil server" commands then add
    @ a command-line option "--errorlog <i>FILENAME</i>" to that
    @ command.
    @ </ol>
    style_footer();
    return;
  }
  if( P("truncate1") && cgi_csrf_safe(1) ){
    fclose(fopen(g.zErrlog,"w"));
  }
  if( P("download") ){
    Blob log;
    blob_read_from_file(&log, g.zErrlog, ExtFILE);
    cgi_set_content_type("text/plain");
    cgi_set_content(&log);
    return;
  }
  szFile = file_size(g.zErrlog, ExtFILE);
  if( P("truncate") ){
    @ <form action="%R/errorlog" method="POST">
    @ <p>Confirm that you want to truncate the %,lld(szFile)-byte error log:
    @ <input type="submit" name="truncate1" value="Confirm">
    @ <input type="submit" name="cancel" value="Cancel">
    @ </form>
    style_footer();
    return;
  }
  @ <p>The server error log at "%h(g.zErrlog)" is %,lld(szFile) bytes in size.
  style_submenu_element("Download", "%R/errorlog?download");
  style_submenu_element("Truncate", "%R/errorlog?truncate");
  in = fossil_fopen(g.zErrlog, "rb");
  if( in==0 ){
    @ <p class='generalError'>Unable top open that file for reading!</p>
    style_footer();
    return;
  }
  if( szFile>MXSHOWLOG && P("all")==0 ){
    @ <form action="%R/errorlog" method="POST">
    @ <p>Only the last %,d(MXSHOWLOG) bytes are shown.
    @ <input type="submit" name="all" value="Show All">
    @ </form>
    fseek(in, -MXSHOWLOG, SEEK_END);
  }
  @ <hr>
  @ <pre>
  while( fgets(z, sizeof(z), in) ){
    @ %h(z)\
  }
  fclose(in);
  @ </pre>
  style_footer();
}
