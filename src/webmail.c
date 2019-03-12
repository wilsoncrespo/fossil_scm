/*
** Copyright (c) 2018 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
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
** Implementation of web pages for managing the email storage tables
** (if they exist):
**
**     emailbox
**     emailblob
**     emailroute
*/
#include "config.h"
#include "webmail.h"
#include <assert.h>


#if INTERFACE

/* Recognized content encodings */
#define EMAILENC_NONE   0         /* No encoding */
#define EMAILENC_B64    1         /* Base64 encoded */
#define EMAILENC_QUOTED 2         /* Quoted printable */

/* An instance of the following object records the location of important
** attributes on a single element in a multipart email message body.
*/
struct EmailBody {
  char zMimetype[32];     /* Mimetype */
  u8 encoding;            /* Type of encoding */
  char *zFilename;        /* From content-disposition: */
  char *zContent;         /* Content.  \0 terminator inserted */
};

/*
** An instance of the following object describes the struture of
** an rfc-2822 email message.
*/
struct EmailToc {
  int nHdr;              /* Number of header lines */
  int nHdrAlloc;         /* Number of header lines allocated */
  char **azHdr;          /* Pointer to header line.  \0 terminator inserted */
  int nBody;             /* Number of body segments */
  int nBodyAlloc;        /* Number of body segments allocated */
  EmailBody *aBody;      /* Location of body information */
};
#endif

/*
** Free An EmailToc object
*/
void emailtoc_free(EmailToc *p){
  int i;
  fossil_free(p->azHdr);
  for(i=0; i<p->nBody; i++){
    fossil_free(p->aBody[i].zFilename);
  }
  fossil_free(p->aBody);
  fossil_free(p);
}

/*
** Allocate a new EmailToc object
*/
EmailToc *emailtoc_alloc(void){
  EmailToc *p = fossil_malloc( sizeof(*p) );
  memset(p, 0, sizeof(*p));
  return p;
}

/*
** Add a new body element to an EmailToc.
*/
EmailBody *emailtoc_new_body(EmailToc *p){
  EmailBody *pNew;
  p->nBody++;
  if( p->nBody>p->nBodyAlloc ){
    p->nBodyAlloc = (p->nBodyAlloc+1)*2;
    p->aBody = fossil_realloc(p->aBody, sizeof(p->aBody[0])*p->nBodyAlloc);
  }
  pNew = &p->aBody[p->nBody-1];
  memset(pNew, 0, sizeof(*pNew));
  return pNew;
}

/*
** Add a new header line to the EmailToc.
*/
void emailtoc_new_header_line(EmailToc *p, char *z){
  p->nHdr++;
  if( p->nHdr>p->nHdrAlloc ){
    p->nHdrAlloc = (p->nHdrAlloc+1)*2;
    p->azHdr = fossil_realloc(p->azHdr, sizeof(p->azHdr[0])*p->nHdrAlloc);
  }
  p->azHdr[p->nHdr-1] = z;
}

/*
** Return the length of a line in an email header.  Continuation lines
** are included.  Hence, this routine returns the number of bytes up to
** and including the first \n character that is followed by something
** other than whitespace.
*/
static int email_line_length(const char *z){
  int i;
  for(i=0; z[i] && (z[i]!='\n' || z[i+1]==' ' || z[i+1]=='\t'); i++){}
  if( z[i]=='\n' ) i++;
  return i;
}

/*
** Look for a parameter of the form NAME=VALUE in the given email
** header line.  Return a copy of VALUE in space obtained from 
** fossil_malloc().  Or return NULL if there is no such parameter.
*/
static char *email_hdr_value(const char *z, const char *zName){
  int nName = (int)strlen(zName);
  int i;
  const char *z2 = strstr(z, zName);
  if( z2==0 ) return 0;
  z2 += nName;
  if( z2[0]!='=' ) return 0;
  z2++;
  if( z2[0]=='"' ){
    z2++;
    for(i=0; z2[i] && z2[i]!='"'; i++){}
    if( z2[i]!='"' ) return 0;
  }else{
    for(i=0; z2[i] && !fossil_isspace(z2[i]); i++){}
  }
  return mprintf("%.*s", i, z2);
}

/*
** Return a pointer to the first non-whitespace character in z
*/
static const char *firstToken(const char *z){
  while( fossil_isspace(*z) ){
    z++;
  }
  return z;
}

/*
** The n-bytes of content in z is a single multipart mime segment
** with its own header and body.  Decode this one segment and add it to p;
**
** Rows of the header of the segment are added to p if bAddHeader is
** true.
*/
LOCAL void emailtoc_add_multipart_segment(
  EmailToc *p,          /* Append the segments here */
  char *z,              /* The body component */
  int bAddHeader        /* True to add header lines to p */
){
  int i, j;
  int n;
  int multipartBody = 0;
  EmailBody *pBody = emailtoc_new_body(p);
  i = 0;
  while( z[i] ){
    n = email_line_length(&z[i]);
    if( (n==2 && z[i]=='\r' && z[i+1]=='\n') || z[i]=='\n' || n==0 ){
      /* This is the blank line at the end of the header */
      i += n;
      break;
    }
    for(j=i+n; j>i && fossil_isspace(z[j-1]); j--){}
    z[j] = 0;
    if( sqlite3_strnicmp(z+i, "Content-Type:", 13)==0 ){
      const char *z2 = firstToken(z+i+13);
      if( z2 && strncmp(z2, "multipart/", 10)==0 ){
        multipartBody = 1;
      }else{
        int j;
        for(j=0; z2[j]=='/' || fossil_isalnum(z2[j]); j++){}
        if( j>=sizeof(pBody->zMimetype) ) j = sizeof(pBody->zMimetype);
        memcpy(pBody->zMimetype, z2, j);
        pBody->zMimetype[j] = 0;
      }
    }
                           /*  123456789 123456789 123456 */
    if( sqlite3_strnicmp(z+i, "Content-Transfer-Encoding:", 26)==0 ){
      const char *z2 = firstToken(z+(i+26));
      if( z2 && sqlite3_strnicmp(z2, "base64", 6)==0 ){
        pBody->encoding = EMAILENC_B64;
                                 /*  123456789 123456 */
      }else if( sqlite3_strnicmp(z2, "quoted-printable", 16)==0 ){
        pBody->encoding = EMAILENC_QUOTED;
      }else{
        pBody->encoding = EMAILENC_NONE;
      }
    }
    if( bAddHeader ){
      emailtoc_new_header_line(p, z+i);
    }else if( sqlite3_strnicmp(z+i, "Content-Disposition:", 20)==0 ){
                                /*   123456789 123456789  */
       fossil_free(pBody->zFilename);
       pBody->zFilename = email_hdr_value(z+i, "filename");
    }
    i += n;
  }
  if( multipartBody ){
    p->nBody--;
    emailtoc_add_multipart(p, z+i);
  }else{
    pBody->zContent = z+i;
  }
}

/*
** The n-bytes of content in z are a multipart/ body component for
** an email message.  Decode this into its individual segments.
**
** The component should start and end with a boundary line.  There
** may be additional boundary lines in the middle.
*/
LOCAL void emailtoc_add_multipart(
  EmailToc *p,          /* Append the segments here */
  char *z               /* The body component.  zero-terminated */
){
  int nB;               /* Size of the boundary string */
  int iStart;           /* Start of the coding region past boundary mark */
  int i;                /* Loop index */
  char *zBoundary = 0;  /* Boundary marker */

  /* Skip forward to the beginning of the boundary mark.  The boundary
  ** mark always begins with "--" */
  while( z[0]!='-' || z[1]!='-' ){
    while( z[0] && z[0]!='\n' ) z++;
    if( z[0]==0 ) return;
    z++;
  }

  /* Find the length of the boundary mark. */
  zBoundary = z;
  for(nB=0; z[nB] && !fossil_isspace(z[nB]); nB++){}
  if( nB==0 ) return;

  z += nB;
  while( fossil_isspace(z[0]) ) z++;
  zBoundary[nB] = 0;
  for(i=iStart=0; z[i]; i++){
    if( z[i]=='\n' && strncmp(z+i+1, zBoundary, nB)==0 ){
      z[i+1] = 0;
      emailtoc_add_multipart_segment(p, z+iStart, 0);
      iStart = i+nB;
      if( z[iStart]=='-' && z[iStart+1]=='-' ) return;
      while( fossil_isspace(z[iStart]) ) iStart++;
      i = iStart;
    }
  }
}

/*
** Compute a table-of-contents (EmailToc) for the email message
** provided on the input.
**
** This routine will cause pEmail to become zero-terminated if it is
** not already.  It will also insert zero characters into parts of
** the message, to delimit the various components.
*/
EmailToc *emailtoc_from_email(Blob *pEmail){
  char *z;
  EmailToc *p = emailtoc_alloc();
  blob_terminate(pEmail);
  z = blob_buffer(pEmail);
  emailtoc_add_multipart_segment(p, z, 1);
  return p;
}

/*
** Inplace-unfolding of an email header line.
**
** Actually - this routine works by converting all contiguous sequences
** of whitespace into a single space character.
*/
static void email_hdr_unfold(char *z){
  int i, j;
  char c;
  for(i=j=0; (c = z[i])!=0; i++){
    if( fossil_isspace(c) ){
      c = ' ';
      if( j && z[j-1]==' ' ) continue;
    }
    z[j++] = c;
  }
  z[j] = 0;
}

/*
** COMMAND: test-decode-email
**
** Usage: %fossil test-decode-email FILE
**
** Read an rfc-2822 formatted email out of FILE, then write a decoding
** to stdout.  Use for testing and validating the email decoder.
*/
void test_email_decode_cmd(void){
  Blob email;
  EmailToc *p;
  int i;
  verify_all_options();
  if( g.argc!=3 ) usage("FILE");
  blob_read_from_file(&email, g.argv[2], ExtFILE);
  p = emailtoc_from_email(&email);
  fossil_print("%d header line and %d content segments\n",
               p->nHdr, p->nBody);
  for(i=0; i<p->nHdr; i++){
    email_hdr_unfold(p->azHdr[i]);
    fossil_print("%3d: %s\n", i, p->azHdr[i]);
  }
  for(i=0; i<p->nBody; i++){
    fossil_print("\nBODY %d mime \"%s\" encoding %d",
                 i, p->aBody[i].zMimetype, p->aBody[i].encoding);
    if( p->aBody[i].zFilename ){
      fossil_print(" filename \"%s\"", p->aBody[i].zFilename);
    }
    fossil_print("\n");
    if( strncmp(p->aBody[i].zMimetype,"text/",5)!=0 ) continue;
    switch( p->aBody[i].encoding ){
      case EMAILENC_B64: {
        int n = 0;
        decodeBase64(p->aBody[i].zContent, &n, p->aBody[i].zContent);
        fossil_print("%s", p->aBody[i].zContent);
        if( n && p->aBody[i].zContent[n-1]!='\n' ) fossil_print("\n");
        break;
      }
      case EMAILENC_QUOTED: {
        int n = 0;
        decodeQuotedPrintable(p->aBody[i].zContent, &n);
        fossil_print("%s", p->aBody[i].zContent);
        if( n && p->aBody[i].zContent[n-1]!='\n' ) fossil_print("\n");
        break;
      }
      default: {
        fossil_print("%s\n", p->aBody[i].zContent);
        break;
      }
    }
  }
  emailtoc_free(p);
  blob_reset(&email);
}

/*
** Add the select/option box to the timeline submenu that shows
** the various email message formats.
*/
static void webmail_f_submenu(void){
  static const char *az[] = {
     "0", "Normal",
     "1", "Decoded",
     "2", "Raw",
  };
  style_submenu_multichoice("f", sizeof(az)/(2*sizeof(az[0])), az, 0);
}

/*
** If the first N characters of z[] are the name of a header field
** that should be shown in "Normal" mode, then return 1.
*/
static int webmail_normal_header(const char *z, int N){
  static const char *az[] = {
    "To",  "Cc",  "Bcc",  "Date", "From",  "Subject",
  };
  int i;
  for(i=0; i<sizeof(az)/sizeof(az[0]); i++){
    if( sqlite3_strnicmp(z, az[i], N)==0 ) return 1;
  }
  return 0;
}

/*
** Paint a page showing a single email message
*/
static void webmail_show_one_message(
  HQuery *pUrl,          /* Calling context */
  int emailid,           /* emailbox.ebid to display */
  const char *zUser      /* User who owns it, or NULL if does not matter */
){
  Blob sql;
  Stmt q;
  int eState = -1;
  int eTranscript = 0;
  char zENum[30];
  style_submenu_element("Index", "%s", url_render(pUrl,"id",0,0,0));
  webmail_f_submenu();
  blob_init(&sql, 0, 0);
  db_begin_transaction();
  blob_append_sql(&sql,
    "SELECT decompress(etxt), estate, emailblob.ets"
    " FROM emailblob, emailbox"
    " WHERE emailid=emsgid AND ebid=%d",
     emailid
  );
  if( zUser ) blob_append_sql(&sql, " AND euser=%Q", zUser);
  db_prepare_blob(&q, &sql);
  blob_reset(&sql);
  style_header("Message %d",emailid);
  if( db_step(&q)==SQLITE_ROW ){
    Blob msg = db_column_text_as_blob(&q, 0);
    int eFormat = atoi(PD("f","0"));
    eState = db_column_int(&q, 1);
    eTranscript = db_column_int(&q, 2);
    if( eFormat==2 ){
      @ <pre>%h(db_column_text(&q, 0))</pre>
    }else{      
      EmailToc *p = emailtoc_from_email(&msg);
      int i, j;
      @ <p>
      for(i=0; i<p->nHdr; i++){
        char *z = p->azHdr[i];
        email_hdr_unfold(z);
        for(j=0; z[j] && z[j]!=':'; j++){}
        if( eFormat==0 && !webmail_normal_header(z, j) ) continue;
        if( z[j]!=':' ){
          @ %h(z)<br>
        }else{
          z[j] = 0;
          @ <b>%h(z):</b> %h(z+j+1)<br>
        }
      }
      for(i=0; i<p->nBody; i++){
        @ <hr><b>Messsage Body #%d(i): %h(p->aBody[i].zMimetype) \
        if( p->aBody[i].zFilename ){
          @ "%h(p->aBody[i].zFilename)"
        }
        @ </b>
        if( eFormat==0 ){
          if( strncmp(p->aBody[i].zMimetype, "text/plain", 10)!=0 ) continue;
          if( p->aBody[i].zFilename ) continue;
        }else{
          if( strncmp(p->aBody[i].zMimetype, "text/", 5)!=0 ) continue;
        }
        switch( p->aBody[i].encoding ){
          case EMAILENC_B64: {
            int n = 0;
            decodeBase64(p->aBody[i].zContent, &n, p->aBody[i].zContent);
            break;
          }
          case EMAILENC_QUOTED: {
            int n = 0;
            decodeQuotedPrintable(p->aBody[i].zContent, &n);
            break;
          }
        }
        @ <pre>%h(p->aBody[i].zContent)</pre>
      }
    }
  }
  db_finalize(&q);

  /* Optionally show the SMTP transcript */
  if( eTranscript>0
   && db_exists("SELECT 1 FROM emailblob WHERE emailid=%d", eTranscript)
  ){
    if( P("ts")==0 ){
      sqlite3_snprintf(sizeof(zENum), zENum, "%d", emailid);
      style_submenu_element("SMTP Transcript","%s",
            url_render(pUrl, "ts", "1", "id", zENum));
    }else{
      db_prepare(&q,
        "SELECT decompress(etxt) FROM emailblob WHERE emailid=%d", eTranscript
      );
      if( db_step(&q)==SQLITE_ROW ){
        const char *zTranscript = db_column_text(&q, 0);
        @ <hr>
        @ <pre>%h(zTranscript)</pre>
      }
      db_finalize(&q);
    }
  }

  if( eState==0 ){
    /* If is message is currently Unread, change it to Read */
    blob_append_sql(&sql,
      "UPDATE emailbox SET estate=1 "
      " WHERE estate=0 AND ebid=%d",
       emailid
    );
    if( zUser ) blob_append_sql(&sql, " AND euser=%Q", zUser);
    db_multi_exec("%s", blob_sql_text(&sql));
    blob_reset(&sql);
    eState = 1;
  }

  url_add_parameter(pUrl, "id", 0);
  sqlite3_snprintf(sizeof(zENum), zENum, "e%d", emailid);
  if( eState==2 ){
    style_submenu_element("Undelete","%s",
      url_render(pUrl,"read","1",zENum,"1"));
  }
  if( eState==1 ){
    style_submenu_element("Delete", "%s",
      url_render(pUrl,"trash","1",zENum,"1"));
    style_submenu_element("Mark As Unread", "%s",
      url_render(pUrl,"unread","1",zENum,"1"));
  }
  if( eState==3 ){
    style_submenu_element("Delete", "%s",
      url_render(pUrl,"trash","1",zENum,"1"));
  }

  db_end_transaction(0);
  style_footer();
  return;
}

/*
** Scan the query parameters looking for parameters with name of the
** form "eN" where N is an integer.  For all such integers, change
** the state of every emailbox entry with ebid==N to eStateNew provided
** that either zUser is NULL or matches.
**
** Or if eNewState==99, then delete the entries.
*/
static void webmail_change_state(int eNewState, const char *zUser){
  Blob sql;
  int sep = '(';
  int i;
  const char *zName;
  int n;
  if( !cgi_csrf_safe(0) ) return;
  blob_init(&sql, 0, 0);
  if( eNewState==99 ){
    blob_append_sql(&sql, "DELETE FROM emailbox WHERE estate==2 AND ebid IN ");
  }else{
    blob_append_sql(&sql, "UPDATE emailbox SET estate=%d WHERE ebid IN ",
                    eNewState);
  }
  for(i=0; (zName = cgi_parameter_name(i))!=0; i++){
    if( zName[0]!='e' ) continue;
    if( !fossil_isdigit(zName[1]) ) continue;
    n = atoi(zName+1);
    blob_append_sql(&sql, "%c%d", sep, n);
    sep = ',';
  }
  if( zUser ){
    blob_append_sql(&sql, ") AND euser=%Q", zUser);
  }else{
    blob_append_sql(&sql, ")");
  }
  if( sep==',' ){
    db_multi_exec("%s", blob_sql_text(&sql));
  }
  blob_reset(&sql);
}


/*
** Add the select/option box to the timeline submenu that shows
** which messages to include in the index.
*/
static void webmail_d_submenu(void){
  static const char *az[] = {
     "0", "InBox",
     "1", "Unread",
     "2", "Trash",
     "3", "Sent",
     "4", "Everything",
  };
  style_submenu_multichoice("d", sizeof(az)/(2*sizeof(az[0])), az, 0);
}

/*
** WEBPAGE:  webmail
**
** This page can be used to read content from the EMAILBOX table
** that contains email received by the "fossil smtpd" command.
**
** Query parameters:
**
**     id=N                 Show a single email entry emailbox.ebid==N
**     f=N                  Display format.  0: decoded 1: raw
**     user=USER            Show mailbox for USER (admin only).
**     user=*               Show mailbox for all users (admin only).
**     d=N                  0: inbox+unread 1: unread-only 2: trash 3: all
**     eN                   Select email entry emailbox.ebid==N
**     trash                Move selected entries to trash (estate=2)
**     read                 Mark selected entries as read (estate=1)
**     unread               Mark selected entries as unread (estate=0)
**  
*/
void webmail_page(void){
  int emailid;
  Stmt q;
  Blob sql;
  int showAll = 0;
  const char *zUser = 0;
  int d = 0;               /* Display mode.  0..3.  d= query parameter */
  int pg = 0;              /* Page number */
  int N = 50;              /* Results per page */
  int got;                 /* Number of results on this page */
  char zPPg[30];           /* Previous page */
  char zNPg[30];           /* Next page */
  HQuery url;
  login_check_credentials();
  if( !login_is_individual() ){
    login_needed(0);
    return;
  }
  if( !db_table_exists("repository","emailbox") ){
    style_header("Webmail Not Available");
    @ <p>This repository is not configured to provide webmail</p>
    style_footer();
    return;
  }
  add_content_sql_commands(g.db);
  emailid = atoi(PD("id","0"));
  url_initialize(&url, "webmail");
  if( g.perm.Admin ){
    zUser = PD("user",g.zLogin);
    if( zUser ){
      url_add_parameter(&url, "user", zUser);
      if( fossil_strcmp(zUser,"*")==0 ){
        showAll = 1;
        zUser = 0;
      }
    }
  }else{
    zUser = g.zLogin;
  }
  if( P("d") ) url_add_parameter(&url, "d", P("d"));
  if( emailid>0 ){
    webmail_show_one_message(&url, emailid, zUser);
    return;
  }
  style_header("Webmail");
  webmail_d_submenu();
  db_begin_transaction();
  if( P("trash")!=0 ) webmail_change_state(2,zUser);
  if( P("unread")!=0 ) webmail_change_state(0,zUser);
  if( P("read")!=0 ) webmail_change_state(1,zUser);
  if( P("purge")!=0 ) webmail_change_state(99,zUser);
  blob_init(&sql, 0, 0);
  blob_append_sql(&sql,
    "CREATE TEMP TABLE tmbox AS "
    "SELECT ebid,"                   /* 0 */
    " efrom,"                        /* 1 */
    " datetime(edate,'unixepoch'),"  /* 2 */
    " estate,"                       /* 3 */
    " esubject,"                     /* 4 */
    " euser"                         /* 5 */
    " FROM emailbox"
  );
  d = atoi(PD("d","0"));
  switch( d ){
    case 0: {   /* Show unread and read */
      blob_append_sql(&sql, " WHERE estate<=1");
      break;
    }
    case 1: {   /* Unread messages only */
      blob_append_sql(&sql, " WHERE estate=0");
      break;
    }
    case 2: {   /* Trashcan only */
      blob_append_sql(&sql, " WHERE estate=2");
      break;
    }
    case 3: {   /* Outgoing email only */
      blob_append_sql(&sql, " WHERE estate=3");
      break;
    }
    case 4: {   /* Everything */
      blob_append_sql(&sql, " WHERE 1");
      break;
    }
  }
  if( showAll ){
    style_submenu_element("My Emails", "%s", url_render(&url,"user",0,0,0));
  }else if( zUser!=0 ){
    style_submenu_element("All Users", "%s", url_render(&url,"user","*",0,0));
    if( fossil_strcmp(zUser, g.zLogin)!=0 ){
      style_submenu_element("My Emails", "%s", url_render(&url,"user",0,0,0));
    }
    if( zUser ){
      blob_append_sql(&sql, " AND euser=%Q", zUser);
    }else{
      blob_append_sql(&sql, " AND euser=%Q", g.zLogin);
    }
  }else{
    if( g.perm.Admin ){
      style_submenu_element("All Users", "%s", url_render(&url,"user","*",0,0));
    }
    blob_append_sql(&sql, " AND euser=%Q", g.zLogin);
  }
  pg = atoi(PD("pg","0"));
  blob_append_sql(&sql, " ORDER BY edate DESC limit %d offset %d", N+1, pg*N);
  db_multi_exec("%s", blob_sql_text(&sql));
  got = db_int(0, "SELECT count(*) FROM tmbox");
  db_prepare(&q, "SELECT * FROM tmbox LIMIT %d", N);
  blob_reset(&sql);
  @ <form action="%R/webmail" method="POST">
  @ <input type="hidden" name="d" value="%d(d)">
  @ <input type="hidden" name="user" value="%h(zUser?zUser:"*")">
  @ <table border="0" width="100%%">
  @ <tr><td align="left">
  if( d==2 ){
    @ <input type="submit" name="read" value="Undelete">
    @ <input type="submit" name="purge" value="Delete Permanently">
  }else{
    @ <input type="submit" name="trash" value="Delete">
    if( d!=1 ){
      @ <input type="submit" name="unread" value="Mark as unread">
    }
    @ <input type="submit" name="read" value="Mark as read">
  }
  @ <button onclick="webmailSelectAll(); return false;">Select All</button>
  @ <a href="%h(url_render(&url,0,0,0,0))">refresh</a>
  @ </td><td align="right">
  if( pg>0 ){
    sqlite3_snprintf(sizeof(zPPg), zPPg, "%d", pg-1);
    @ <a href="%s(url_render(&url,"pg",zPPg,0,0))">&lt; Newer</a>&nbsp;&nbsp;
  }
  if( got>50 ){
    sqlite3_snprintf(sizeof(zNPg),zNPg,"%d",pg+1);
    @ <a href="%s(url_render(&url,"pg",zNPg,0,0))">Older &gt;</a></td>
  }
  @ </table>
  @ <table>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zId = db_column_text(&q,0);
    const char *zFrom = db_column_text(&q, 1);
    const char *zDate = db_column_text(&q, 2);
    const char *zSubject = db_column_text(&q, 4);
    if( zSubject==0 || zSubject[0]==0 ) zSubject = "(no subject)";
    @ <tr>
    @ <td><input type="checkbox" class="webmailckbox" name="e%s(zId)"></td>
    @ <td>%h(zFrom)</td>
    @ <td><a href="%h(url_render(&url,"id",zId,0,0))">%h(zSubject)</a> \
    @ %s(zDate)</td>
    if( showAll ){
      const char *zTo = db_column_text(&q,5);
      @ <td><a href="%h(url_render(&url,"user",zTo,0,0))">%h(zTo)</a></td>
    }
    @ </tr>
  }
  db_finalize(&q);
  @ </table>
  @ </form>
  @ <script>
  @ function webmailSelectAll(){
  @   var x = document.getElementsByClassName("webmailckbox");
  @   for(i=0; i<x.length; i++){
  @     x[i].checked = true;
  @   }
  @ }
  @ </script>
  style_footer();
  db_end_transaction(0);
}

/*
** WEBPAGE:  emailblob
**
** This page, accessible only to administrators, allows easy viewing of
** the emailblob table - the table that contains the text of email messages
** both inbound and outbound, and transcripts of SMTP sessions.
**
**    id=N          Show the text of emailblob with emailid==N
**    
*/
void webmail_emailblob_page(void){
  int id = atoi(PD("id","0"));
  Stmt q;
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed(0);
    return;
  }
  add_content_sql_commands(g.db);
  style_header("emailblob table");
  if( id>0 ){
    style_submenu_element("Index", "%R/emailblob");
    @ <ul>
    db_prepare(&q, "SELECT emailid FROM emailblob WHERE ets=%d", id);
    while( db_step(&q)==SQLITE_ROW ){
      int id = db_column_int(&q, 0);
      @ <li> <a href="%R/emailblob?id=%d(id)">emailblob entry %d(id)</a>
    }
    db_finalize(&q);
    db_prepare(&q, "SELECT euser, estate FROM emailbox WHERE emsgid=%d", id);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zUser = db_column_text(&q, 0);
      int e = db_column_int(&q, 1);
      @ <li> emailbox for %h(zUser) state %d(e)
    }
    db_finalize(&q);
    db_prepare(&q, "SELECT efrom, eto FROM emailoutq WHERE emsgid=%d", id);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zFrom = db_column_text(&q, 0);
      const char *zTo = db_column_text(&q, 1);
      @ <li> emailoutq message body from %h(zFrom) to %h(zTo)
    }
    db_finalize(&q);
    db_prepare(&q, "SELECT efrom, eto FROM emailoutq WHERE ets=%d", id);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zFrom = db_column_text(&q, 0);
      const char *zTo = db_column_text(&q, 1);
      @ <li> emailoutq transcript from %h(zFrom) to %h(zTo)
    }
    db_finalize(&q);
    @ </ul>
    @ <hr>
    db_prepare(&q, "SELECT decompress(etxt) FROM emailblob WHERE emailid=%d",
               id);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zContent = db_column_text(&q, 0);
      @ <pre>%h(zContent)</pre>
    }
    db_finalize(&q);
  }else{
    style_submenu_element("emailoutq table","%R/emailoutq");
    db_prepare(&q,
       "SELECT emailid, enref, ets, datetime(etime,'unixepoch'), esz,"
       " length(etxt)"
       " FROM emailblob ORDER BY etime DESC, emailid DESC");
    @ <table border="1" cellpadding="5" cellspacing="0" class="sortable" \
    @ data-column-types='nnntkk'>
    @ <thead><tr><th> emailid <th> enref <th> ets <th> etime \
    @ <th> uncompressed <th> compressed </tr></thead><tbody>
    while( db_step(&q)==SQLITE_ROW ){
      int id = db_column_int(&q, 0);
      int nref = db_column_int(&q, 1);
      int ets = db_column_int(&q, 2);
      const char *zDate = db_column_text(&q, 3);
      int sz = db_column_int(&q,4);
      int csz = db_column_int(&q,5);
      @ <tr>
      @  <td align="right"><a href="%R/emailblob?id=%d(id)">%d(id)</a>
      @  <td align="right">%d(nref)</td>
      if( ets>0 ){
        @  <td align="right">%d(ets)</td>
      }else{
        @  <td>&nbsp;</td>
      }
      @  <td>%h(zDate)</td>
      @  <td align="right" data-sortkey='%08x(sz)'>%,d(sz)</td>
      @  <td align="right" data-sortkey='%08x(csz)'>%,d(csz)</td>
      @ </tr>
    }
    @ </tbody></table>
    db_finalize(&q);
    style_table_sorter();
  }
  style_footer();
}

/*
** WEBPAGE:  emailoutq
**
** This page, accessible only to administrators, allows easy viewing of
** the emailoutq table - the table that contains the email messages
** that are queued for transmission via SMTP.
*/
void webmail_emailoutq_page(void){
  Stmt q;
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed(0);
    return;
  }
  add_content_sql_commands(g.db);
  style_header("emailoutq table");
  style_submenu_element("emailblob table","%R/emailblob");
  db_prepare(&q,
     "SELECT edomain, efrom, eto, emsgid, "
     "       datetime(ectime,'unixepoch'),"
     "       datetime(nullif(emtime,0),'unixepoch'),"
     "       ensend, ets"
     " FROM emailoutq"
  );
  @ <table border="1" cellpadding="5" cellspacing="0" class="sortable" \
  @ data-column-types='tttnttnn'>
  @ <thead><tr><th> edomain <th> efrom <th> eto <th> emsgid \
  @ <th> ectime <th> emtime <th> ensend <th> ets </tr></thead><tbody>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zDomain = db_column_text(&q, 0);
    const char *zFrom = db_column_text(&q, 1);
    const char *zTo = db_column_text(&q, 2);
    int emsgid = db_column_int(&q, 3);
    const char *zCTime = db_column_text(&q, 4);
    const char *zMTime = db_column_text(&q, 5);
    int ensend = db_column_int(&q, 6);
    int ets = db_column_int(&q, 7);
    @ <tr>
    @  <td>%h(zDomain)
    @  <td>%h(zFrom)
    @  <td>%h(zTo)
    @  <td align="right"><a href="%R/emailblob?id=%d(emsgid)">%d(emsgid)</a>
    @  <td>%h(zCTime)
    @  <td>%h(zMTime)
    @  <td align="right">%d(ensend)
    if( ets>0 ){
      @  <td align="right"><a href="%R/emailblob?id=%d(ets)">%d(ets)</a></td>
    }else{
      @  <td>&nbsp;</td>
    }
  }
  @ </tbody></table>
  db_finalize(&q);
  style_table_sorter();
  style_footer();
}
