/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
  Copyright (C) 2009-2018 Olof Hagsand and Benny Holmgren

  This file is part of CLIXON.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  Alternatively, the contents of this file may be used under the terms of
  the GNU General Public License Version 3 or later (the "GPL"),
  in which case the provisions of the GPL are applicable instead
  of those above. If you wish to allow use of your version of this file only
  under the terms of the GPL, and not to allow others to
  use your version of this file under the terms of Apache License version 2, 
  indicate your decision by deleting the provisions above and replace them with
  the  notice and other provisions required by the GPL. If you do not delete
  the provisions above, a recipient may use your version of this file under
  the terms of any one of the Apache License version 2 or the GPL.

  ***** END LICENSE BLOCK *****
  
 */

/*
 * See rfc8040

 * sudo apt-get install libfcgi-dev
 * gcc -o fastcgi fastcgi.c -lfcgi

 * sudo su -c "/www-data/clixon_restconf -Df /usr/local/etc/routing.xml " -s /bin/sh www-data

 * This is the interface:
 * api/data/profile=<name>/metric=<name> PUT data:enable=<flag>
 * api/test
   +----------------------------+--------------------------------------+
   | 100 Continue               | POST accepted, 201 should follow     |
   | 200 OK                     | Success with response message-body   |
   | 201 Created                | POST to create a resource success    |
   | 204 No Content             | Success without response message-    |
   |                            | body                                 |
   | 304 Not Modified           | Conditional operation not done       |
   | 400 Bad Request            | Invalid request message              |
   | 401 Unauthorized           | Client cannot be authenticated       |
   | 403 Forbidden              | Access to resource denied            |
   | 404 Not Found              | Resource target or resource node not |
   |                            | found                                |
   | 405 Method Not Allowed     | Method not allowed for target        |
   |                            | resource                             |
   | 409 Conflict               | Resource or lock in use              |
   | 412 Precondition Failed    | Conditional method is false          |
   | 413 Request Entity Too     | too-big error                        |
   | Large                      |                                      |
   | 414 Request-URI Too Large  | too-big error                        |
   | 415 Unsupported Media Type | non RESTCONF media type              |
   | 500 Internal Server Error  | operation-failed                     |
   | 501 Not Implemented        | unknown-operation                    |
   | 503 Service Unavailable    | Recoverable server error             |
   +----------------------------+--------------------------------------+
Mapping netconf error-tag -> status code
                 +-------------------------+-------------+
                 | <error&#8209;tag>       | status code |
                 +-------------------------+-------------+
                 | in-use                  | 409         |
                 | invalid-value           | 400         |
                 | too-big                 | 413         |
                 | missing-attribute       | 400         |
                 | bad-attribute           | 400         |
                 | unknown-attribute       | 400         |
                 | bad-element             | 400         |
                 | unknown-element         | 400         |
                 | unknown-namespace       | 400         |
                 | access-denied           | 403         |
                 | lock-denied             | 409         |
                 | resource-denied         | 409         |
                 | rollback-failed         | 500         |
                 | data-exists             | 409         |
                 | data-missing            | 409         |
                 | operation-not-supported | 501         |
                 | operation-failed        | 500         |
                 | partial-operation       | 500         |
                 | malformed-message       | 400         |
                 +-------------------------+-------------+

 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <assert.h>
#include <time.h>
#include <fcgi_stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clixon/clixon.h>

#include "restconf_lib.h"
#include "restconf_methods.h"

/*! REST OPTIONS method
 * According to restconf
 * @param[in]  h      Clixon handle
 * @param[in]  r      Fastcgi request handle
 * @code
 *  curl -G http://localhost/restconf/data/interfaces/interface=eth0
 * @endcode                     
 * Minimal support: 
 * 200 OK
 * Allow: HEAD,GET,PUT,DELETE,OPTIONS              
 */
int
api_data_options(clicon_handle h,
		 FCGX_Request *r)
{
    clicon_debug(1, "%s", __FUNCTION__);
    FCGX_SetExitStatus(200, r->out); /* OK */
    FCGX_FPrintF(r->out, "Allow: OPTIONS,HEAD,GET,POST,PUT,DELETE\r\n");
    FCGX_FPrintF(r->out, "\r\n");
    return 0;
}

/*! Generic GET (both HEAD and GET)
 */
static int
api_data_get_gen(clicon_handle h,
		 FCGX_Request *r,
		 cvec         *pcvec,
		 int           pi,
		 cvec         *qvec,
		 int           head)
{
    int        retval = -1;
    cbuf      *path = NULL;
    cbuf      *cbx = NULL;
    cxobj    **vec = NULL;
    yang_spec *yspec;
    cxobj     *xret = NULL;
    cxobj     *xerr;
    cxobj     *xtag;
    cbuf      *cbj = NULL;;
    int        code;
    const char *reason_phrase;
    char      *media_accept;
    int        use_xml = 0; /* By default use JSON */

    clicon_debug(1, "%s", __FUNCTION__);
    media_accept = FCGX_GetParam("HTTP_ACCEPT", r->envp);
    if (strcmp(media_accept, "application/yang-data+xml")==0)
	use_xml++;
    yspec = clicon_dbspec_yang(h);
    if ((path = cbuf_new()) == NULL)
        goto done;
    cprintf(path, "/");
    if (api_path2xpath_cvv(yspec, pcvec, pi, path) < 0){
	notfound(r);
	goto done;
    }
    clicon_debug(1, "%s path:%s", __FUNCTION__, cbuf_get(path));
    if (clicon_rpc_get(h, cbuf_get(path), &xret) < 0){
	notfound(r);
	goto done;
    }
#if 0 /* DEBUG */
    {
	cbuf *cb = cbuf_new();
	xml2json_cbuf(cb, xret, 1);
	clicon_debug(1, "%s xret:%s", __FUNCTION__, cbuf_get(cb));
	cbuf_free(cb);
    }
#endif
    if ((xerr = xpath_first(xret, "/rpc-error")) != NULL){
	if ((cbj = cbuf_new()) == NULL)
	    goto done;
	if ((xtag = xpath_first(xerr, "/error-tag")) == NULL){
	    notfound(r); /* bad reply? */
	    goto done;
	}
	code = restconf_err2code(xml_body(xtag));
	if ((reason_phrase = restconf_code2reason(code)) == NULL)
	    reason_phrase="";
	clicon_debug(1, "%s code:%d reason phrase:%s", 
		     __FUNCTION__, code, reason_phrase);

	if (xml_name_set(xerr, "error") < 0)
	    goto done;
	if (xml2json_cbuf(cbj, xerr, 1) < 0)
	    goto done;
	FCGX_FPrintF(r->out, "Status: %d %s\r\n", code, reason_phrase);
	FCGX_FPrintF(r->out, "Content-Type: application/yang-data+json\r\n\r\n");
	FCGX_FPrintF(r->out, "\r\n");
	FCGX_FPrintF(r->out, "{\r\n");
	FCGX_FPrintF(r->out, "  \"ietf-restconf:errors\" : {\r\n");
	FCGX_FPrintF(r->out, "    %s", cbuf_get(cbj));
	FCGX_FPrintF(r->out, "  }\r\n");
	FCGX_FPrintF(r->out, "}\r\n");
	goto ok;
    }
    if ((cbx = cbuf_new()) == NULL)
	goto done;
    FCGX_SetExitStatus(200, r->out); /* OK */
    FCGX_FPrintF(r->out, "Content-Type: application/yang-data+%s\r\n", use_xml?"xml":"json");
    FCGX_FPrintF(r->out, "\r\n");
    if (head)
	goto ok;
    clicon_debug(1, "%s name:%s child:%d", __FUNCTION__, xml_name(xret), xml_child_nr(xret));

    clicon_debug(1, "%s xretnr:%d", __FUNCTION__, xml_child_nr(xret));
    if (use_xml){
	if (clicon_xml2cbuf(cbx, xret, 0, 1) < 0) /* Dont print top object?  */
	    goto done;
    }
    else{
	vec = xml_childvec_get(xret);
	if (xml2json_cbuf_vec(cbx, vec, xml_child_nr(xret), 0) < 0)
	    goto done;
    }
    clicon_debug(1, "%s cbuf:%s", __FUNCTION__, cbuf_get(cbx));
    FCGX_FPrintF(r->out, "%s", cbx?cbuf_get(cbx):"");
    FCGX_FPrintF(r->out, "\r\n\r\n");
 ok:
    retval = 0;
 done:
    clicon_debug(1, "%s retval:%d", __FUNCTION__, retval);
    if (cbx)
        cbuf_free(cbx);
    if (cbj)
        cbuf_free(cbj);
    if (path)
	cbuf_free(path);
    if (xret)
	xml_free(xret);
    return retval;
}

/*! REST HEAD method
 * @param[in]  h      Clixon handle
 * @param[in]  r      Fastcgi request handle
 * @param[in]  pcvec  Vector of path ie DOCUMENT_URI element 
 * @param[in]  pi     Offset, where path starts  
 * @param[in]  qvec   Vector of query string (QUERY_STRING)
   The HEAD method is sent by the client to retrieve just the header fields 
   that would be returned for the comparable GET method, without the 
   response message-body. 
 * Relation to netconf: none                        
 */
int
api_data_head(clicon_handle h,
	     FCGX_Request *r,
             cvec         *pcvec,
             int           pi,
             cvec         *qvec)
{
    return api_data_get_gen(h, r, pcvec, pi, qvec, 1);
}

/*! REST GET method
 * According to restconf 
 * @param[in]  h      Clixon handle
 * @param[in]  r      Fastcgi request handle
 * @param[in]  pcvec  Vector of path ie DOCUMENT_URI element 
 * @param[in]  pi     Offset, where path starts  
 * @param[in]  qvec   Vector of query string (QUERY_STRING)
 * @code
 *  curl -G http://localhost/restconf/data/interfaces/interface=eth0
 * @endcode                                     
 * XXX: cant find a way to use Accept request field to choose Content-Type  
 *      I would like to support both xml and json.           
 * Request may contain                                        
 *     Accept: application/yang.data+json,application/yang.data+xml   
 * Response contains one of:                           
 *     Content-Type: application/yang-data+xml    
 *     Content-Type: application/yang-data+json  
 * NOTE: If a retrieval request for a data resource representing a YANG leaf-
 * list or list object identifies more than one instance, and XML
 * encoding is used in the response, then an error response containing a
 * "400 Bad Request" status-line MUST be returned by the server.
 * Netconf: <get-config>, <get>                        
 */
int
api_data_get(clicon_handle h,
	     FCGX_Request *r,
             cvec         *pcvec,
             int           pi,
             cvec         *qvec)
{
    return api_data_get_gen(h, r, pcvec, pi, qvec, 0);
}

/*! REST POST  method 
 * @param[in]  h      CLIXON handle
 * @param[in]  r      Fastcgi request handle
 * @param[in]  api_path According to restconf (Sec 3.5.3.1 in rfc8040)
 * @param[in]  pcvec  Vector of path ie DOCUMENT_URI element
 * @param[in]  pi     Offset, where to start pcvec
 * @param[in]  qvec   Vector of query string (QUERY_STRING)
 * @param[in]  data   Stream input data
 * @note We map post to edit-config create. 
 POST:
   target resource type is datastore --> create a top-level resource
   target resource type is  data resource --> create child resource

   The message-body MUST contain exactly one instance of the
   expected data resource.  The data model for the child tree is the
   subtree, as defined by YANG for the child resource.

   If the POST method succeeds, a "201 Created" status-line is returned
   and there is no response message-body.  A "Location" header
   identifying the child resource that was created MUST be present in
   the response in this case.

   If the data resource already exists, then the POST request MUST fail
   and a "409 Conflict" status-line MUST be returned.
 * Netconf:  <edit-config> (nc:operation="create") | invoke an RPC operation        * @example
 */
int
api_data_post(clicon_handle h,
	      FCGX_Request *r, 
	      char         *api_path, 
	      cvec         *pcvec, 
	      int           pi,
	      cvec         *qvec, 
	      char         *data)
{
    enum operation_type op = OP_CREATE;
    int        retval = -1;
    int        i;
    cxobj     *xdata = NULL;
    cxobj     *xtop = NULL; /* xpath root */
    cbuf      *cbx = NULL;
    cxobj     *xbot = NULL;
    cxobj     *x;
    yang_node *y = NULL;
    yang_spec *yspec;
    cxobj     *xa;
    char      *media_content_type;
    int        parse_xml = 0; /* By default expect and parse JSON */

    clicon_debug(1, "%s api_path:\"%s\" json:\"%s\"",
		 __FUNCTION__, 
		 api_path, data);
    media_content_type = FCGX_GetParam("HTTP_CONTENT_TYPE", r->envp);
    if (strcmp(media_content_type, "application/yang-data+xml")==0)
	parse_xml++;
    if ((yspec = clicon_dbspec_yang(h)) == NULL){
	clicon_err(OE_FATAL, 0, "No DB_SPEC");
	goto done;
    }
    for (i=0; i<pi; i++)
	api_path = index(api_path+1, '/');
    /* Create config top-of-tree */
    if ((xtop = xml_new("config", NULL, NULL)) == NULL)
	goto done;
    xbot = xtop;
    if (api_path && api_path2xml(api_path, yspec, xtop, 0, &xbot, &y) < 0)
	goto done;
    /* Parse input data as json or xml into xml */
    if (parse_xml){
	if (xml_parse_string(data, NULL, &xdata) < 0){
	    clicon_debug(1, "%s json parse fail: %s", __FUNCTION__, data);
	    goto done;
	}
    }
    else if (json_parse_str(data, &xdata) < 0){
	clicon_debug(1, "%s json parse fail: %s", __FUNCTION__, data);
	goto done;
    }
    /* Add xdata to xbot */
    x = NULL;
    while ((x = xml_child_each(xdata, x, CX_ELMNT)) != NULL) {
	if ((xa = xml_new("operation", x, NULL)) == NULL)
	    goto done;
	xml_type_set(xa, CX_ATTR);
	if (xml_value_set(xa,  xml_operation2str(op)) < 0)
	    goto done;
	if (xml_addsub(xbot, x) < 0)
	    goto done;
    }
    if ((cbx = cbuf_new()) == NULL)
	goto done;
    if (clicon_xml2cbuf(cbx, xtop, 0, 0) < 0)
	goto done;
    clicon_debug(1, "%s xml: %s",__FUNCTION__, cbuf_get(cbx));
    if (clicon_rpc_edit_config(h, "candidate", 
			       OP_NONE,
			       cbuf_get(cbx)) < 0){
	//	notfound(r); /* XXX */
	conflict(r);
	goto done;
    }
    if (clicon_rpc_validate(h, "candidate") < 0){
	if (clicon_rpc_discard_changes(h) < 0)
	    goto done;
	badrequest(r);
	retval = 0;
	goto done;
    }
    if (clicon_rpc_commit(h) < 0)
	goto done;
    FCGX_SetExitStatus(201, r->out); /* Created */
    FCGX_FPrintF(r->out, "Content-Type: text/plain\r\n");
    // XXX api_path can be null    FCGX_FPrintF(r->out, "Location: %s\r\n", api_path);
    FCGX_FPrintF(r->out, "\r\n");
    retval = 0;
 done:
    clicon_debug(1, "%s retval:%d", __FUNCTION__, retval);
    if (xtop)
	xml_free(xtop);
    if (xdata)
	xml_free(xdata);
     if (cbx)
	cbuf_free(cbx); 
   return retval;
}

/*! Generic REST PUT  method 
 * @param[in]  h      CLIXON handle
 * @param[in]  r      Fastcgi request handle
 * @param[in]  api_path According to restconf (Sec 3.5.3.1 in rfc8040)
 * @param[in]  pcvec  Vector of path ie DOCUMENT_URI element
 * @param[in]  pi     Offset, where to start pcvec
 * @param[in]  qvec   Vector of query string (QUERY_STRING)
 * @param[in]  data   Stream input data
 * @example
      curl -X PUT -d '{"enabled":"false"}' http://127.0.0.1/restconf/data/interfaces/interface=eth1
 *
 PUT:
  if the PUT request creates a new resource,
   a "201 Created" status-line is returned.  If an existing resource is
   modified, a "204 No Content" status-line is returned.

 * Netconf:  <edit-config> (nc:operation="create/replace")
 */
int
api_data_put(clicon_handle h,
	     FCGX_Request *r, 
	     char         *api_path, 
	     cvec         *pcvec, 
	     int           pi,
	     cvec         *qvec, 
	     char         *data)
{
    int        retval = -1;
    enum operation_type op = OP_REPLACE;
    int        i;
    cxobj     *xdata = NULL;
    cbuf      *cbx = NULL;
    cxobj     *x;
    cxobj     *xbot = NULL;
    cxobj     *xtop = NULL;
    cxobj     *xp;
    yang_node *y = NULL;
    yang_spec *yspec;
    cxobj     *xa;
    char      *media_content_type;
    int        parse_xml = 0; /* By default expect and parse JSON */

    clicon_debug(1, "%s api_path:\"%s\" json:\"%s\"",
		 __FUNCTION__, 
		 api_path, data);
    media_content_type = FCGX_GetParam("HTTP_CONTENT_TYPE", r->envp);
    if (strcmp(media_content_type, "application/yang-data+xml")==0)
	parse_xml++;
    if ((yspec = clicon_dbspec_yang(h)) == NULL){
	clicon_err(OE_FATAL, 0, "No DB_SPEC");
	goto done;
    }
    for (i=0; i<pi; i++)
	api_path = index(api_path+1, '/');
    /* Create config top-of-tree */
    if ((xtop = xml_new("config", NULL, NULL)) == NULL)
	goto done;
    xbot = xtop;
    if (api_path && api_path2xml(api_path, yspec, xtop, 0, &xbot, &y) < 0)
	goto done;
    /* Parse input data as json or xml into xml */
    if (parse_xml){
	if (xml_parse_string(data, NULL, &xdata) < 0){
	    clicon_debug(1, "%s json parse fail: %s", __FUNCTION__, data);
	    goto done;
	}
    }
    else if (json_parse_str(data, &xdata) < 0){
	clicon_debug(1, "%s json parse fail: %s", __FUNCTION__, data);
	goto done;
    }
    if (xml_child_nr(xdata) != 1){
	badrequest(r);
	retval = 0;
	goto done;
    }
    x = xml_child_i(xdata,0);
    if ((xa = xml_new("operation", x, NULL)) == NULL)
	goto done;
    xml_type_set(xa, CX_ATTR);
    if (xml_value_set(xa,  xml_operation2str(op)) < 0)
	goto done;
    /* Replace xbot with x */    
    xp = xml_parent(xbot);
    xml_purge(xbot);
    if (xml_addsub(xp, x) < 0) 	
	goto done;
    if ((cbx = cbuf_new()) == NULL)
	goto done;
    if (clicon_xml2cbuf(cbx, xtop, 0, 0) < 0)
	goto done;
    clicon_debug(1, "%s xml: %s api_path:%s",__FUNCTION__, cbuf_get(cbx), api_path);
    if (clicon_rpc_edit_config(h, "candidate", 
			       OP_NONE,
			       cbuf_get(cbx)) < 0){
	notfound(r);
	goto done;
    }
    
    if (clicon_rpc_validate(h, "candidate") < 0){
	if (clicon_rpc_discard_changes(h) < 0)
	    goto done;
	badrequest(r);
	retval = 0;
	goto done;
    }
    if (clicon_rpc_commit(h) < 0)
	goto done;
    FCGX_SetExitStatus(201, r->out); /* Created */
    FCGX_FPrintF(r->out, "Content-Type: text/plain\r\n");
    FCGX_FPrintF(r->out, "\r\n");
    retval = 0;
 done:
    clicon_debug(1, "%s retval:%d", __FUNCTION__, retval);
    if (xtop)
	xml_free(xtop);
    if (xdata)
	xml_free(xdata);
     if (cbx)
	cbuf_free(cbx); 
   return retval;

}

/*! Generic REST PATCH  method 
 * @param[in]  h      CLIXON handle
 * @param[in]  r      Fastcgi request handle
 * @param[in]  api_path According to restconf (Sec 3.5.3.1 in rfc8040)
 * @param[in]  pcvec  Vector of path ie DOCUMENT_URI element
 * @param[in]  pi     Offset, where to start pcvec
 * @param[in]  qvec   Vector of query string (QUERY_STRING)
 * @param[in]  data   Stream input data
 * Netconf:  <edit-config> (nc:operation="merge")      
 */
int
api_data_patch(clicon_handle h,
	      FCGX_Request *r, 
	      char         *api_path, 
	      cvec         *pcvec, 
	      int           pi,
	      cvec         *qvec, 
	      char         *data)
{
    notimplemented(r);
    return 0;
}

/*! Generic REST DELETE method 
 * @param[in]  h      CLIXON handle
 * @param[in]  r      Fastcgi request handle
 * @param[in]  api_path According to restconf (Sec 3.5.3.1 in rfc8040)
 * @param[in]  pi     Offset, where path starts
 * Example:
 *  curl -X DELETE http://127.0.0.1/restconf/data/interfaces/interface=eth0
 * Netconf:  <edit-config> (nc:operation="delete")      
 */
int
api_data_delete(clicon_handle h,
		FCGX_Request *r, 
		char         *api_path,
		int           pi)
{
    int        retval = -1;
    int        i;
    cxobj     *xtop = NULL; /* xpath root */
    cxobj     *xbot = NULL;
    cxobj     *xa;
    cbuf      *cbx = NULL;
    yang_node *y = NULL;
    yang_spec *yspec;
    enum operation_type op = OP_DELETE;

    clicon_debug(1, "%s api_path:%s", __FUNCTION__, api_path);
    if ((yspec = clicon_dbspec_yang(h)) == NULL){
	clicon_err(OE_FATAL, 0, "No DB_SPEC");
	goto done;
    }
    for (i=0; i<pi; i++)
	api_path = index(api_path+1, '/');
    /* Create config top-of-tree */
    if ((xtop = xml_new("config", NULL, NULL)) == NULL)
	goto done;
    xbot = xtop;
    if (api_path && api_path2xml(api_path, yspec, xtop, 0, &xbot, &y) < 0)
	goto done;
    if ((xa = xml_new("operation", xbot, NULL)) == NULL)
	goto done;
    xml_type_set(xa, CX_ATTR);
    if (xml_value_set(xa,  xml_operation2str(op)) < 0)
	goto done;
    if ((cbx = cbuf_new()) == NULL)
	goto done;

    if (clicon_xml2cbuf(cbx, xtop, 0, 0) < 0)
	goto done;
    if (clicon_rpc_edit_config(h, "candidate", 
			       OP_NONE, 
			       cbuf_get(cbx)) < 0){
	notfound(r);
	goto done;
    }
    if (clicon_rpc_commit(h) < 0)
	goto done;
    FCGX_SetExitStatus(201, r->out);
    FCGX_FPrintF(r->out, "Content-Type: text/plain\r\n");
    FCGX_FPrintF(r->out, "\r\n");
    retval = 0;
 done:
    if (cbx)
	cbuf_free(cbx); 
    if (xtop)
	xml_free(xtop);
    clicon_debug(1, "%s retval:%d", __FUNCTION__, retval);
   return retval;
}

/*! REST operation POST method 
 * @param[in]  h      CLIXON handle
 * @param[in]  r      Fastcgi request handle
 * @param[in]  pcvec  Vector of path ie DOCUMENT_URI element
 * @param[in]  pi     Offset, where to start pcvec
 * @param[in]  qvec   Vector of query string (QUERY_STRING)
 * @param[in]  data   Stream input data
 * @note We map post to edit-config create. 

      POST {+restconf}/operations/<operation>


 */
int
api_operation_post(clicon_handle h,
		   FCGX_Request *r, 
		   char         *path, 
		   cvec         *pcvec, 
		   int           pi,
		   cvec         *qvec, 
		   char         *data)
{
    int        retval = -1;
    int        i;
    char      *oppath = path;
    yang_stmt *yrpc = NULL;
    yang_spec *yspec;
    yang_stmt *yinput;
    yang_stmt *youtput;
    cxobj     *xdata = NULL;
    cxobj     *xret = NULL;
    cbuf      *cbx = NULL;
    cxobj     *xtop = NULL; /* xpath root */
    cxobj     *xbot = NULL;
    yang_node *y = NULL;
    cxobj     *xinput;
    cxobj     *xoutput;
    cxobj     *x;
    char      *media_content_type;
    int        parse_xml = 0; /* By default expect and parse JSON */
    char      *media_accept;
    int        use_xml = 0; /* By default return JSON */

    clicon_debug(1, "%s json:\"%s\" path:\"%s\"", __FUNCTION__, data, path);
    if ((media_accept = FCGX_GetParam("HTTP_ACCEPT", r->envp)) &&
	strcmp(media_accept, "application/yang-data+xml")==0)
	use_xml++;
    if ((media_content_type = FCGX_GetParam("HTTP_CONTENT_TYPE", r->envp)) &&
	strcmp(media_content_type, "application/yang-data+xml")==0)
	parse_xml++;
    clicon_debug(1, "%s accept:\"%s\" content-type:\"%s\"", 
		 __FUNCTION__, media_accept, media_content_type);
    if ((yspec = clicon_dbspec_yang(h)) == NULL){
	clicon_err(OE_FATAL, 0, "No DB_SPEC");
	goto done;
    }
    for (i=0; i<pi; i++)
	oppath = index(oppath+1, '/');
    clicon_debug(1, "%s oppath: %s", __FUNCTION__, oppath);

    /* Find yang rpc statement, return yang rpc statement if found */
    if (yang_abs_schema_nodeid(yspec, oppath, &yrpc) < 0)
	goto done;
    if (yrpc == NULL){
	retval = notfound(r); 
	goto done;
    }
    /* Create an xml message: 
     * <"rpc"><operation><input-args>...
     * eg <rpc><fib-route><name>
     */
    /* Create config top-of-tree */
    if ((xtop = xml_new("rpc", NULL, NULL)) == NULL)
	goto done;
    xbot = xtop;
    if (api_path2xml(oppath, yspec, xtop, 1, &xbot, &y) < 0)
	goto done;
    if (data && strlen(data)){
	/* Parse input data as json or xml into xml */
	if (parse_xml){
	    if (xml_parse_string(data, NULL, &xdata) < 0){
		clicon_debug(1, "%s json parse fail: %s", __FUNCTION__, data);
		goto done;
	    }
	}
	else if (json_parse_str(data, &xdata) < 0){
	    clicon_debug(1, "%s json parse fail: %s", __FUNCTION__, data);
	    goto done;
	}
	/* xdata should have format <top><input> */
	if ((xinput = xpath_first(xdata, "/input")) != NULL){
	    /* Add all input under <rpc>path */
	    x = NULL;
	    while (xml_child_nr(xinput)){
		x = xml_child_i(xinput, 0);
		if (xml_addsub(xbot, x) < 0) 	
		    goto done;
	    }
	    if ((yinput = yang_find((yang_node*)yrpc, Y_INPUT, NULL)) != NULL){
		xml_spec_set(xinput, yinput); /* needed for xml_spec_populate */
		if (xml_apply(xinput, CX_ELMNT, xml_spec_populate, yinput) < 0)
		    goto done;
		if (xml_apply(xinput, CX_ELMNT, 
			      (xml_applyfn_t*)xml_yang_validate_all, NULL) < 0)
		    goto done;
		if (xml_yang_validate_add(xinput, NULL) < 0)
		    goto done;
	    }
	}
    }
    /* Non-standard: add cookie as attribute for backend
     */
    {
	cxobj *xa;
	char *cookie;
	char *cookieval = NULL;
	
	if ((cookie = FCGX_GetParam("HTTP_COOKIE", r->envp)) != NULL &&
	    get_user_cookie(cookie, "c-user", &cookieval) ==0){
	    if ((xa = xml_new("id", xtop, NULL)) == NULL)
		goto done;
	    xml_type_set(xa, CX_ATTR);
	    if (xml_value_set(xa,  cookieval) < 0)
		goto done;
	    if (cookieval)
		free(cookieval);
	}
    }
    /* Send to backend */
    if (clicon_rpc_netconf_xml(h, xtop, &xret, NULL) < 0)
	goto done;
    if ((cbx = cbuf_new()) == NULL)
	goto done;
    xoutput=xpath_first(xret, "/");
    if ((youtput = yang_find((yang_node*)yrpc, Y_OUTPUT, NULL)) != NULL &&
	xoutput){
	xml_name_set(xoutput, "output");
	clicon_debug(1, "%s xoutput:%s", __FUNCTION__, cbuf_get(cbx));
	cbuf_reset(cbx);
	xml_spec_set(xoutput, youtput); /* needed for xml_spec_populate */
	if (xml_apply(xoutput, CX_ELMNT, xml_spec_populate, youtput) < 0)
	    goto done;
	if (xml_apply(xoutput, CX_ELMNT, 
		      (xml_applyfn_t*)xml_yang_validate_all, NULL) < 0)
	    goto done;
	if (xml_yang_validate_add(xoutput, NULL) < 0)
	    goto done;
    }
    /* Sanity check of outgoing XML */
    FCGX_SetExitStatus(200, r->out); /* OK */
    FCGX_FPrintF(r->out, "Content-Type: application/yang-data+%s\r\n", use_xml?"xml":"json");
    FCGX_FPrintF(r->out, "\r\n");
    if (xoutput){
	if (use_xml){
	    if (clicon_xml2cbuf(cbx, xoutput, 0, 1) < 0)
		goto done;
	}
	else
	    if (xml2json_cbuf(cbx, xoutput, 1) < 0)
		goto done;
	clicon_debug(1, "%s xoutput:%s", __FUNCTION__, cbuf_get(cbx));
	FCGX_FPrintF(r->out, "%s", cbx?cbuf_get(cbx):"");
	FCGX_FPrintF(r->out, "\r\n\r\n");
    }
    retval = 0;
 done:
    clicon_debug(1, "%s retval:%d", __FUNCTION__, retval);
    if (xdata)
	xml_free(xdata);
    if (xtop)
	xml_free(xtop);
    if (xret)
	xml_free(xret);
     if (cbx)
	cbuf_free(cbx); 
   return retval;
}
