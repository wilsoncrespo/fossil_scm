/*
** Copyright (c) 2017 D. Richard Hipp
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
** This file contains code used to manage a cookie that stores user-specific
** display preferences for the web interface.
**
** cookie_parse(void);
**
**    Read and parse the display preferences cookie.
**
** cookie_read_parameter(zQP, zPName);
**
**    If query parameter zQP does not exist but zPName does exist in
**    the parsed cookie, then initialize zQP to hold the same value
**    as the zPName element in the parsed cookie.
**
** cookie_write_parameter(zQP, zPName, zDefault);
**
**    If query parameter zQP exists and if it has a different value from
**    the zPName parameter in the parsed cookie, then replace the value of
**    zPName with the value of zQP.  If zQP exists but zPName does not
**    exist, then zPName is created.  If zQP does not exist or if it has
**    the same value as zPName, then this routine is a no-op.
**
** cookie_link_parameter(zQP, zPName, zDefault);
**
**    This does both cookie_read_parameter() and cookie_write_parameter()
**    all at once.
**
** cookie_render();
**
**    If any prior calls to cookie_write_parameter() have changed the
**    value of the user preferences cookie, this routine will cause the
**    new cookie value to be included in the HTTP header for the current
**    web page.  This routine is a destructor for this module and should
**    be called once.
**
** char *cookie_value(zPName, zDefault);
**
**    Look up the value of a cookie parameter zPName.  Return zDefault if
**    there is no display preferences cookie or if zPName does not exist.
*/
#include "cookies.h"
#include <assert.h>
#include <string.h>

#if INTERFACE
/* the standard name of the display settings cookie for fossil */
# define DISPLAY_SETTINGS_COOKIE    "fossil_display_settings"
#endif


/*
** State information private to this module
*/
#define COOKIE_NPARAM  10
static struct {
  char *zCookieValue;         /* Value of the user preferences cookie */
  int bChanged;               /* True if any value has changed */
  int bIsInit;                /* True after initialization */
  int nParam;                 /* Number of parameters in the cookie */
  struct {
    const char *zPName;         /* Name of a parameter */
    char *zPValue;              /* Value of that parameter */
  } aParam[COOKIE_NPARAM];
} cookies;

/* Initialize this module by parsing the content of the cookie named
** by DISPLAY_SETTINGS_COOKIE
*/
void cookie_parse(void){
  char *z;
  if( cookies.bIsInit ) return;
  z = (char*)P(DISPLAY_SETTINGS_COOKIE);
  if( z==0 ) z = "";
  cookies.zCookieValue = z = mprintf("%s", z);
  cookies.bIsInit = 1;
  while( cookies.nParam<COOKIE_NPARAM ){
    while( fossil_isspace(z[0]) ) z++;
    if( z[0]==0 ) break;
    cookies.aParam[cookies.nParam].zPName = z;
    while( *z && *z!='=' && *z!=',' ){ z++; }
    if( *z=='=' ){
      *z = 0;
      z++;
      cookies.aParam[cookies.nParam].zPValue = z;
      while( *z && *z!=',' ){ z++; }
      if( *z ){
        *z = 0;
        z++;
      }
      dehttpize(cookies.aParam[cookies.nParam].zPValue);
    }else{
      if( *z ){ *z++ = 0; }
      cookies.aParam[cookies.nParam].zPValue = "";
    }
    cookies.nParam++;
  }
}

#define COOKIE_READ  1
#define COOKIE_WRITE 2
static void cookie_readwrite(
  const char *zQP,        /* Name of the query parameter */
  const char *zPName,     /* Name of the cooking setting */
  const char *zDflt,      /* Default value for the query parameter */
  int flags               /* READ or WRITE or both */
){
  const char *zQVal = P(zQP);
  int i;
  cookie_parse();
  for(i=0; i<cookies.nParam && strcmp(zPName,cookies.aParam[i].zPName); i++){}
  if( zQVal==0 && (flags & COOKIE_READ)!=0 && i<cookies.nParam ){
    cgi_set_parameter_nocopy(zQP, cookies.aParam[i].zPValue, 1);
    return;
  }
  if( zQVal==0 ) zQVal = zDflt;
  if( (flags & COOKIE_WRITE)!=0
   && i<COOKIE_NPARAM
   && (i==cookies.nParam || strcmp(zQVal, cookies.aParam[i].zPValue))
  ){
    if( i==cookies.nParam ){
      cookies.aParam[i].zPName = zPName;
      cookies.nParam++;
    }
    cookies.aParam[i].zPValue = (char*)zQVal;
    cookies.bChanged = 1;
  }
}

/* If query parameter zQP is missing, initialize it using the zPName
** value from the user preferences cookie
*/
void cookie_read_parameter(const char *zQP, const char *zPName){
  cookie_readwrite(zQP, zPName, 0, COOKIE_READ);
}

/* Update the zPName value of the user preference cookie to match
** the value of query parameter zQP.
*/
void cookie_write_parameter(
  const char *zQP,
  const char *zPName,
  const char *zDflt
){
  cookie_readwrite(zQP, zPName, zDflt, COOKIE_WRITE);
}

/* Use the zPName user preference value as a default for zQP and record
** any changes to the zQP value back into the cookie.
*/
void cookie_link_parameter(
  const char *zQP,       /* The query parameter */
  const char *zPName,    /* The name of the cookie value */
  const char *zDflt      /* Default value for the parameter */
){
  cookie_readwrite(zQP, zPName, zDflt, COOKIE_READ|COOKIE_WRITE);
}

/* Update the user preferences cookie, if necessary, and shut down this
** module
*/
void cookie_render(void){
  if( cookies.bChanged && P("udc")!=0 ){
    Blob new;
    int i;
    blob_init(&new, 0, 0);
    for(i=0;i<cookies.nParam;i++){
      if( i>0 ) blob_append(&new, ",", 1);
      blob_appendf(&new, "%s=%T",
          cookies.aParam[i].zPName, cookies.aParam[i].zPValue);
    }
    cgi_set_cookie(DISPLAY_SETTINGS_COOKIE, blob_str(&new), 0, 31536000);
  }
  cookies.bIsInit = 0;
}

/* Return the value of a preference cookie.
*/
const char *cookie_value(const char *zPName, const char *zDefault){
  int i;
  assert( zPName!=0 );
  cookie_parse();
  for(i=0; i<cookies.nParam && strcmp(zPName,cookies.aParam[i].zPName); i++){}
  return i<cookies.nParam ? cookies.aParam[i].zPValue : zDefault;
}

/*
** WEBPAGE:  cookies
**
** Show the current display settings contained in the
** "fossil_display_settings" cookie.
*/
void cookie_page(void){
  int i;
  if( PB("clear") ){
    cgi_set_cookie(DISPLAY_SETTINGS_COOKIE, "", 0, 1);
    cgi_replace_parameter(DISPLAY_SETTINGS_COOKIE, "");
  }
  cookie_parse();
  style_header("User Preference Cookie Values");
  if( cookies.nParam ){
    style_submenu_element("Clear", "%R/cookies?clear");
  }
  @ <p>The following are user preference settings held in the
  @ "fossil_display_settings" cookie.
  @ <ul>
  @ <li>Raw cookie value: "%h(PD("fossil_display_settings",""))"
  for(i=0; i<cookies.nParam; i++){
    @ <li>%h(cookies.aParam[i].zPName): "%h(cookies.aParam[i].zPValue)"
  }
  @ </ul>
  style_footer();
}
