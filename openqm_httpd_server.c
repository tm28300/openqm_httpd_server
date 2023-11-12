/* 
*/ 

#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <microhttpd.h>

#include <qmdefs.h>
#include <qmclilib.h>

#include <ctype.h>
#include <libconfig.h>
#include <pcre.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>

#include "openqm_httpd_server.h"

// Types

struct openqm_req_data_struct {
   char *auth_type;
   char *hostname;
   char *header_in;
   char *query_string;
   char *remote_info;
   char *remote_user;
   char *method;
   char *uri;
   char *server_info;
};

struct openqm_resp_data_struct {
   char *http_output;
   char  http_status [4];
   char *header_out;
};

struct headerin_info_struct {
   char **ptr_headerin_dynarray;
   bool   error_status;
};

struct querystring_info_struct {
   char                          **ptr_querystring_dynarray;
   unsigned int                    http_error;
   struct connection_info_struct  *connection_info;
};

// Declaration

static bool add_key_value_2dynarray (char **dynarray, const char *key, const char *value);
static int iterate_post (void *postinfo_cls, enum MHD_ValueKind kind, const char *key, const char *filename, const char *content_type, const char *transfer_encoding, const char *data, uint64_t off, size_t size);
static int iterate_header (void *headerininfo_cls, enum MHD_ValueKind kind, const char *key, const char *value);
static int iterate_querystring (void *querystringinfo_cls, enum MHD_ValueKind kind, const char *key, const char *value);
static void request_completed (void *cls, struct MHD_Connection *connection, void **postinfo_cls, enum MHD_RequestTerminationCode toe);
static unsigned int openqm_init_req (struct openqm_req_data_struct *openqm_req_data, struct MHD_Connection *connection, struct connection_info_struct *connection_info, const char *url, const char *method);
static bool openqm_init_resp (struct openqm_resp_data_struct *openqm_resp_data);
static struct MHD_Response *make_default_error_page (struct MHD_Connection *connection, unsigned int status_code);
static int ohs_send_response (struct MHD_Connection *connection, unsigned int http_return_code, struct MHD_Response *response);
static int openqm_to_connection (void *cls, struct MHD_Connection *connection, const char *url, const char *method, const char *version, const char *upload_data, size_t *upload_data_size, void **postinfo_cls);

// Globals constants

//static const size_t ipv4_string_length = 16;
//static const size_t ipv6_string_length = 40;
static const size_t post_max_size = 32768;
static const size_t headerin_max_size = 16384;
static const size_t querystring_max_size = 16384;
static const char procotol_http [] = "http";
static const char procotol_https [] = "https";
static const size_t post_buffer_size = post_max_size / 32;
static const char common_error_page [] = "<html><head><title>Error</title></head><body><p>%s</p></body></html>";

// Functions

void abort_message (const char* error_message)
{
   // Only add the error message in log
   syslog (LOG_USER | LOG_ERR, "%s", error_message);
#ifdef OHS_DEBUG
   printf ("abort_message : %s\n", error_message);
#endif
}

bool add_key_value_2dynarray (char **dynarray,
                              const char *key,
                              const char *value)
{
   int mv_key;
   if (QMLocate (key, *dynarray, 1, 1, 0, &mv_key, "AL")) {
      if (value != NULL && strlen (value) != 0) {
         char* new_dynarray;
         char* old_value = QMExtract (*dynarray, 2, mv_key, 0);
         if (old_value) {
            char* new_value = realloc (old_value, strlen (old_value) + strlen (value) + 1);
            if (new_value == NULL) {
               QMFree (old_value);
               return false;
            }
            strcat (new_value, value);
            new_dynarray = QMReplace (*dynarray, 2, mv_key, 0, new_value);
            free (new_value);
         }
         else
         {
            new_dynarray = QMReplace (*dynarray, 2, mv_key, 0, value);
         }
         QMFree (*dynarray);
         *dynarray = new_dynarray;
      }
   }
   else
   {
      char* new_dynarray = QMIns (*dynarray, 1, mv_key, 0, key);
      QMFree (*dynarray);
      *dynarray = new_dynarray;
      new_dynarray = QMIns (*dynarray, 2, mv_key, 0, value == NULL ? "" : value);
      QMFree (*dynarray);
      *dynarray = new_dynarray;
   }
   return true;
}

int iterate_post (void *postinfo_cls,
                  enum MHD_ValueKind kind,
                  const char *key,
                  const char *filename,
                  const char *content_type,
                  const char *transfer_encoding,
                  const char *data,
                  uint64_t off,
                  size_t size)
{
   struct post_info_struct *post_info = postinfo_cls;
   size_t new_len = strlen (post_info->post_dynarray) + strlen (key) + strlen(data) + 1;

   if (new_len > post_max_size) {
      post_info->error_status = true;
      return MHD_NO;
   }
   if (!add_key_value_2dynarray (&post_info->post_dynarray, key, data)) {
      post_info->error_status = true;
      return MHD_NO;
   }
   return MHD_YES;
}

int iterate_header (void *headerininfo_cls,
                    enum MHD_ValueKind kind,
                    const char *key,
                    const char *value)
{
   // Ignore Host header pass in hostname
   if (strcasecmp (key, "host") != 0) {
      struct headerin_info_struct *headerin_info = headerininfo_cls;
      size_t new_len = strlen (*headerin_info->ptr_headerin_dynarray) + strlen (key) + strlen (value) + 1;

      if (new_len > headerin_max_size) {
         headerin_info->error_status = true;
         return MHD_NO;
      }
      if (!add_key_value_2dynarray (headerin_info->ptr_headerin_dynarray, key, value)) {
         headerin_info->error_status = true;
         return MHD_NO;
      }
   }
   return MHD_YES;
}

int iterate_querystring (void *querystringinfo_cls,
                         enum MHD_ValueKind kind,
                         const char *key,
                         const char *value)
{
   struct querystring_info_struct *querystring_info = querystringinfo_cls;
   size_t new_len = strlen (*querystring_info->ptr_querystring_dynarray) + strlen (key) + (value == NULL ? 0 : strlen (value)) + 1;

   if (!check_get_param_authorized (key, querystring_info->connection_info)) {
      char error_message_detail [256];

      snprintf (error_message_detail, sizeof (error_message_detail), "Get parameter not allowed (%s)", key);
      abort_message (error_message_detail);
      querystring_info->http_error = MHD_HTTP_BAD_REQUEST;
      return MHD_NO;
   }
   if (new_len > querystring_max_size) {
      abort_message ("Query string too large");
      querystring_info->http_error = MHD_HTTP_BAD_REQUEST;
      return MHD_NO;
   }
   if (!add_key_value_2dynarray (querystring_info->ptr_querystring_dynarray, key, value)) {
      abort_message ("Full memory when retreive query string");
      querystring_info->http_error = MHD_HTTP_INTERNAL_SERVER_ERROR;
      return MHD_NO;
   }
   return MHD_YES;
}

void request_completed (void *cls,
                        struct MHD_Connection *connection,
                        void **connection_info_cls,
                        enum MHD_RequestTerminationCode toe)
{
   struct connection_info_struct *connection_info = *connection_info_cls;

#ifdef OHS_DEBUG
   printf ("Start request_completed\n");
#endif
   if (connection_info != NULL) {
      if (connection_info->post_info != NULL) {
         if (connection_info->post_info->connection_type == ct_post) {
            MHD_destroy_post_processor (connection_info->post_info->post_processor);
            if (connection_info->post_info->post_dynarray) {
               free (connection_info->post_info->post_dynarray);
            }
         }
         free (connection_info->post_info);
      }
      free (connection_info);
      *connection_info_cls = NULL;
   }
#ifdef OHS_DEBUG
   printf ("End request_completed\n");
#endif
}

unsigned int openqm_init_req (struct openqm_req_data_struct *openqm_req_data, struct MHD_Connection *connection, struct connection_info_struct *connection_info, const char *url, const char *method)
{
   // Initialise to null QM string
   openqm_req_data->header_in = malloc (1);
   if (openqm_req_data->header_in == NULL) {
      abort_message ("Full memory to initialize input buffer");
      return MHD_HTTP_INTERNAL_SERVER_ERROR;
   }
   *openqm_req_data->header_in = '\0';
   openqm_req_data->query_string = malloc (1);
   if (openqm_req_data->query_string == NULL) {
      abort_message ("Full memory to initialize input buffer");
      return MHD_HTTP_INTERNAL_SERVER_ERROR;
   }
   *openqm_req_data->query_string = '\0';
   openqm_req_data->server_info = malloc (1);
   if (openqm_req_data->server_info == NULL) {
      abort_message ("Full memory to initialize input buffer");
      return MHD_HTTP_INTERNAL_SERVER_ERROR;
   }
   *openqm_req_data->server_info = '\0';

   // Copy string
   openqm_req_data->method = strdup (method);
   openqm_req_data->uri = strdup (url);
   if (openqm_req_data->method == NULL || openqm_req_data->uri == NULL) {
      abort_message ("Full memory when copying request data");
      return MHD_HTTP_INTERNAL_SERVER_ERROR;
   }

   // Request hostname
   const char *header_hostname = MHD_lookup_connection_value (connection, MHD_HEADER_KIND, "Host");
   if (header_hostname != NULL) {
      openqm_req_data->hostname = strdup (header_hostname);
   }

   // Process headers in
   struct headerin_info_struct headerin_info;
   headerin_info.ptr_headerin_dynarray = &openqm_req_data->header_in;
   headerin_info.error_status = false;
   MHD_get_connection_values (connection, MHD_HEADER_KIND, &iterate_header, &headerin_info);
   if (headerin_info.error_status) {
      abort_message ("Full memory when copying header in");
      return MHD_HTTP_INTERNAL_SERVER_ERROR;
   }

   // Retreive data from GET method
   struct querystring_info_struct querystring_info;
   querystring_info.ptr_querystring_dynarray = &openqm_req_data->query_string;
   querystring_info.http_error = 0;
   querystring_info.connection_info = connection_info;
   MHD_get_connection_values (connection, MHD_GET_ARGUMENT_KIND, &iterate_querystring, &querystring_info);
   if (querystring_info.http_error) {
      return querystring_info.http_error;
   }

   // Remote info (IP address ":" Port)
   openqm_req_data->remote_info = malloc(1);
   openqm_req_data->remote_info[0] = '\0';
   /* TODO
   MHD_OPTION_NOTIFY_CONNECTION
   MHD_get_connection_info (, MHD_CONNECTION_INFO_SOCKET_CONTEXT, );
   switch (connection->addrlen) {
      case sizeof (struct sockaddr_in):
         // IPv4
         openqm_req_data->remote_info = malloc(ipv4_string_length + 6);
         if (openqm_req_data->remote_info) {
            const struct sockaddr_in in_addr* = (struct sockaddr_in *)connection->addr;
            inet_ntop (AF_INET, &in_addr->sin_addr, openqm_req_data->remote_info, ipv_string_length + 6);
            sprintf (openqm_req_data->remote_info + strlen (openqm_req_data->remote_info), ":%d", in_addr->sin_port);
         }
         break;
      case sizeof (struct sockaddr_in6):
         // IPv6
         openqm_req_data->remote_info = malloc(ipv6_string_length + 6);
         if (openqm_req_data->remote_info) {
            const struct sockaddr_in6 in6_addr* = (struct sockaddr_in6 *)connection->addr;
            inet_ntop (AF_INET6, &in6_addr->sin6_addr, openqm_req_data->remote_info, ipv6_string_length + 6);
            sprintf (openqm_req_data->remote_info + strlen (openqm_req_data->remote_info), ":%d", in6_addr->sin6_port);
         }
         break;
      default:
         openqm_req_data->remote_info = malloc(1);
         if (openqm_req_data->remote_info) {
            openqm_req_data->remote_info[1] = '\0';
         }
         break;
   }*/
   if (openqm_req_data->remote_info == NULL ) {
      abort_message ("Full memory when retreiving remote info");
      return MHD_HTTP_INTERNAL_SERVER_ERROR;
   }

   // Authentication type and remove user
   // TODO basic and digest authentication
   openqm_req_data->auth_type = strdup ("NONE");
   if (openqm_req_data->auth_type == NULL) {
      abort_message ("Full memory when extract remove user");
      return MHD_HTTP_INTERNAL_SERVER_ERROR;
   }

   // Server info
   const char *protocol_name;
   if (MHD_get_connection_info (connection, MHD_CONNECTION_INFO_PROTOCOL) == NULL) {
      protocol_name = procotol_http;
   }
   else {
      protocol_name = procotol_https;
   }
   if (!add_key_value_2dynarray (&openqm_req_data->server_info, "protocol", protocol_name)) {
      abort_message ("Full memory when retreiving server info");
      return MHD_HTTP_INTERNAL_SERVER_ERROR;
   }
   // TODO : IP address, port
   return 0;
}

bool openqm_init_resp (struct openqm_resp_data_struct *openqm_resp_data)
{
   openqm_resp_data->http_output = malloc (65536);
   if (!openqm_resp_data->http_output) {
      abort_message ("Full memory to initialize output buffer");
      return false;
   }
   strcpy (openqm_resp_data->http_output, "*65535");
   openqm_resp_data->header_out = malloc (16384);
   if (!openqm_resp_data->header_out) {
      abort_message ("Full memory to initialize output buffer");
      return false;
   }
   strcpy (openqm_resp_data->header_out, "*16383");
   return true;
}

struct MHD_Response *make_default_error_page (struct MHD_Connection *connection,
                                               unsigned int status_code)
{
   struct MHD_Response *response = NULL;
   //char *error_page = NULL;
   char error_message [256];

#ifdef OHS_DEBUG
   printf ("Default error page %u\n", status_code);
#endif

   switch (status_code) {
      case MHD_HTTP_BAD_REQUEST:           // 400
         strcpy (error_message, "Bad request");
         break;
      case MHD_HTTP_NOT_FOUND:             // 404
         strcpy (error_message, "Page not found");
         break;
      case MHD_HTTP_METHOD_NOT_ALLOWED:    // 405
         strcpy (error_message, "Method not allowed");
         break;
      case MHD_HTTP_INTERNAL_SERVER_ERROR: // 500
         strcpy (error_message, "Internal server error");
         break;
      case MHD_HTTP_SERVICE_UNAVAILABLE:   // 503
         strcpy (error_message, "Service unavailable");
         break;
      default:
         snprintf (error_message, sizeof (error_message), "Unknown error %u", status_code);
         status_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
         break;
   }

   char *page_to_send;
   size_t page_length = sizeof (common_error_page) + strlen (error_message);

   page_to_send = malloc (page_length);
   if (! page_to_send) {
      abort_message ("Full memory when generate default error page");
      return NULL;
   }

   snprintf (page_to_send, page_length, common_error_page, error_message);
   response = MHD_create_response_from_buffer (strlen (page_to_send), page_to_send, MHD_RESPMEM_MUST_FREE);
   if (response == NULL) {
      abort_message ("Can't create buffer for default error page");
   }
   else {
      MHD_add_response_header (response, "Content-type", "text/html; charset=utf-8");
   }
   return response;
}

int ohs_send_response (struct MHD_Connection *connection, unsigned int http_return_code, struct MHD_Response *response)
{
   int return_status = MHD_NO;

   if (response != NULL) {
#ifdef OHS_DEBUG
      printf ("Before queue response\n");
#endif
      return_status = MHD_queue_response (connection,
                                          http_return_code,
                                          response);
#ifdef OHS_DEBUG
      printf ("After queue response\n");
#endif
      MHD_destroy_response (response);
#ifdef OHS_DEBUG
      printf ("After destroy response\n");
#endif
   }
   return return_status;
}

int openqm_to_connection (void *cls,
                          struct MHD_Connection *connection,
                          const char *url,
                          const char *method,
                          const char *version,
                          const char *upload_data,
                          size_t *upload_data_size,
                          void **connection_info_cls)
{
   unsigned int http_return_code = 0;
   struct MHD_Response *response = NULL;

   if (*connection_info_cls == NULL) {
      struct connection_info_struct *connection_info;

      connection_info = malloc (sizeof (struct connection_info_struct));
      if (connection_info == NULL) {
         abort_message ("Full memory when initialize a connection");
         return MHD_NO;
      }
      connection_info->post_info = NULL;
      connection_info->subr = NULL;
      connection_info->method_authorized_length = -1;
      connection_info->method_authorized = NULL;
      connection_info->get_param_authorized_length = -1;
      connection_info->get_param_authorized = NULL;
      *connection_info_cls = (void *) connection_info;

      http_return_code = extract_subroutine_name_from_url (url, connection_info);
      if (http_return_code != 0) {
         response = make_default_error_page (connection, http_return_code);
         return ohs_send_response (connection, http_return_code, response);
      }

      if (!check_method_authorized (method, connection_info)) {
         http_return_code = MHD_HTTP_METHOD_NOT_ALLOWED;
         response = make_default_error_page (connection, http_return_code);
         return ohs_send_response (connection, http_return_code, response);
      }

      struct post_info_struct *post_info;

      post_info = malloc (sizeof (struct post_info_struct));
      if (post_info == NULL) {
         abort_message ("Full memory when initialize post data structure");
         return MHD_NO;
      }
      connection_info->post_info = post_info;
      post_info->post_dynarray = NULL;
      post_info->post_processor = NULL;
      post_info->error_status = false;

      // OpenQM empty string
      post_info->post_dynarray = malloc (1);
      if (post_info->post_dynarray == NULL) {
         return MHD_NO;
      }
      post_info->post_dynarray[0] = '\0';
      if (strcmp (method, "GET") == 0) {
         post_info->connection_type = ct_get;
      }
      else {
         post_info->connection_type = ct_post;
         post_info->post_processor = MHD_create_post_processor (connection,
                                                                post_buffer_size,
                                                                iterate_post,
                                                                (void *) post_info);
         if (post_info->post_processor == NULL) {
            free (post_info->post_dynarray);
            free (post_info);
            return MHD_NO;
         }
      }
      return MHD_YES;
   }

   struct connection_info_struct *connection_info = *connection_info_cls;

   if (connection_info->post_info->connection_type == ct_post && *upload_data_size != 0) {
      MHD_post_process (connection_info->post_info->post_processor,
                        upload_data,
                        *upload_data_size);
      *upload_data_size = 0;

      return MHD_YES;
   }
   if (connection_info->post_info->error_status) {
      abort_message ("Full memory when receiving post data");
      return MHD_NO;
   }

   /*
    * 1 AUTH.TYPE -> MHD_basic_auth_get_username_password, MHD_digest_auth_get_username, certificat?
    * - CONTENT.LENGTH -> Longueur de la page de retour, généré automatiquement par le serveur
    * - CONTENT.TYPE -> Retourné dans header_out
    * - DOCUMENT.ROOT
    * - GATEWAY.INTERFACE
    * 2 HOSTNAME
    * 3 HTTP headers : MHD_get_connection_values MHD_HEADER_KIND, entre autre pour les variables suivantes
    *  HTTP.ACCEPT
    *  HTTP.ACCEPT.CHARSET
    *  HTTP.ACCEPT.ENCODING
    *  HTTP.ACCEPT.LANGUAGE
    *  HTTP.CACHE.CONTROL
    *  HTTP.CONNECTION
    *  HTTP.COOKIE
    *  HTTP.HOST
    *  HTTP.KEEP.ALIVE
    *  HTTP.REFERER
    *  HTTP.UA.CPU
    *  HTTP.USER.AGENT
    *  HTTP.X.FORWARDED.FOR
    *  HTTP.X.FORWARDED.HOST
    *  HTTP.X.FORWARDED.SERVER
    * - PATH.INFO
    * - PATH.TRANSLATED
    * 4 QUERY.STRING -> ?
    * 5 Post data -> connection_info->post_info->post_dynarray
    * 6 Remote info
    *  REMOTE.ADDR -> connection->addr
    *  REMOTE.PORT -> connection->addr
    * 7 REMOTE.USER -> MHD_basic_auth_get_username_password, MHD_digest_auth_get_username, certificat?
    * 8 REQUEST.METHOD -> method
    * 9 REQUEST.URI -> url
    * - SCRIPT.FILENAME
    * - SCRIPT.NAME
    * 10 Server info
    * - SERVER.ADDR (IP address)
    * - SERVER.ADMIN
    * - SERVER.NAME
    * - SERVER.PORT
    *  SERVER.PROTOCOL (http or https)
    * - SERVER.SIGNATURE
    * - SERVER.SOFTWARE
    * - UNIQUE.ID
    * - USER.AGENT
    * 11 HTTP.OUTPUT (out)
    * 12 HTTP.STATUS (out)
    * 13 HTTP.HEADER (out)
    */

#ifdef OHS_DEBUG
   printf ("Starting\n");
#endif

   struct openqm_req_data_struct openqm_req_data;
   struct openqm_resp_data_struct openqm_resp_data;

   openqm_req_data.auth_type = NULL;
   openqm_req_data.hostname = NULL;
   openqm_req_data.header_in = NULL;
   openqm_req_data.query_string = NULL;
   openqm_req_data.remote_info = NULL;
   openqm_req_data.remote_user = NULL;
   openqm_req_data.method = NULL;
   openqm_req_data.uri = NULL;
   openqm_req_data.server_info = NULL;
   openqm_resp_data.http_output = NULL;
   strcpy (openqm_resp_data.http_status, "*3");
   openqm_resp_data.header_out = NULL;

   http_return_code = openqm_init_req (&openqm_req_data, connection, connection_info, url, method);

   if (http_return_code == 0 && openqm_init_resp (&openqm_resp_data)) {
      if (openqm_req_data.hostname == NULL) {
         abort_message ("Hostname not provided");
         http_return_code = MHD_HTTP_BAD_REQUEST;
      }
      else if (!QMConnectLocal (config_openqm_account)) {
         char error_message_detail [256];

         snprintf (error_message_detail, sizeof (error_message_detail), "Can't connect to OpenQM account %s: %s", config_openqm_account, QMError ());
         abort_message (error_message_detail);
         http_return_code = MHD_HTTP_SERVICE_UNAVAILABLE;
      }
      else {
#ifdef OHS_DEBUG
         printf ("Connected to OpenQM\n");
#endif

#ifdef OHS_DEBUG
         printf ("Calling to OpenQM\n");
#endif
         QMCall (connection_info->subr, 
                 13,
                 openqm_req_data.auth_type,                 // 1
                 openqm_req_data.hostname,                  // 2
                 openqm_req_data.header_in,                 // 3
                 openqm_req_data.query_string,              // 4
                 connection_info->post_info->post_dynarray, // 5
                 openqm_req_data.remote_info,               // 6
                 openqm_req_data.remote_user,               // 7
                 openqm_req_data.method,                    // 8
                 openqm_req_data.uri,                       // 9
                 openqm_req_data.server_info,               // 10
                 openqm_resp_data.http_output,              // 11
                 openqm_resp_data.http_status,              // 12
                 openqm_resp_data.header_out                // 13
                 );

#ifdef OHS_DEBUG
         printf ("OpenQM call return\n");
#endif
         QMDisconnect ();
#ifdef OHS_DEBUG
         printf ("Disconnected from OpenQM\n");
#endif

         // Check if the routine update the status
         if (strcmp (openqm_resp_data.http_status, "*3") == 0) {
            char error_message_detail [256];

            snprintf (error_message_detail, sizeof (error_message_detail), "The routine %s didn't update http status", connection_info->subr);
            abort_message (error_message_detail);
            http_return_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
         }
         else {
            // Return status
            http_return_code = atoi (openqm_resp_data.http_status);
            if (!http_return_code) {
               http_return_code = MHD_HTTP_OK;
            }

            // Complete web page
            response = MHD_create_response_from_buffer (strlen (openqm_resp_data.http_output), openqm_resp_data.http_output, MHD_RESPMEM_MUST_FREE);
            if (response == NULL) {
               http_return_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
            }
            else {
               openqm_resp_data.http_output = NULL;
            }

            // Process headers out
            char* header_out_fields = QMExtract (openqm_resp_data.header_out, 1, 0, 0);
            char* header_out_values = QMExtract (openqm_resp_data.header_out, 2, 0, 0);
            int field_numbers_hout = QMDcount (header_out_fields, FIELD_MARK_STRING);

            for (int field_number = 0 ; field_number < field_numbers_hout ; field_number++) 
            {
               char* temp_field = QMExtract (header_out_fields, 1, field_number + 1, 1);
               char* temp_value = QMExtract (header_out_values, 1, field_number + 1, 1);
               MHD_add_response_header (response, temp_field, temp_value);
#ifdef OHS_DEBUG
               printf ("Header out %s=%s\n", temp_field, temp_value);
#endif
               QMFree (temp_field);
               QMFree (temp_value);
            }
            QMFree (header_out_fields);
            QMFree (header_out_values);
         }
      }
   }
   else { // if (init_req && init_resp
      if (http_return_code == 0) {
         http_return_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
      }
   }
   QMFree (openqm_req_data.header_in);
   QMFree (openqm_req_data.query_string);
   QMFree (openqm_req_data.server_info);
   if (openqm_req_data.method) {
      free (openqm_req_data.method);
   }
   if (openqm_req_data.uri) {
      free (openqm_req_data.uri);
   }
   if (openqm_req_data.hostname) {
      free (openqm_req_data.hostname);
   }
   if (openqm_req_data.remote_info) {
      free (openqm_req_data.remote_info);
   }
   if (openqm_req_data.auth_type) {
      free (openqm_req_data.auth_type);
   }
   if (openqm_resp_data.http_output) {
      free (openqm_resp_data.http_output);
   }
   if (openqm_resp_data.header_out) {
      free (openqm_resp_data.header_out);
   }

   if (response == NULL) {
      response = make_default_error_page (connection, http_return_code);
   }
   return ohs_send_response (connection, http_return_code, response);
}

int main ()
{
   config_init (&config_openqm_httpd_server);
   if (!ohs_config_read ()) {
      ohs_config_free ();
      config_destroy (&config_openqm_httpd_server);
      return 2;
   }

   struct MHD_Daemon *daemon;

   daemon = MHD_start_daemon (MHD_USE_INTERNAL_POLLING_THREAD,
                              config_http_port,
                              NULL,                        // apc (check client)
                              NULL,                        // apc_cls
                              &openqm_to_connection,       // dh (handler for all url)
                              NULL,                        // dh_cls
                              MHD_OPTION_NOTIFY_COMPLETED,
                              &request_completed,          // Cleanup when completed
                              NULL,
                              MHD_OPTION_END);
   if (daemon == NULL) {
      ohs_config_free ();
      return 1;
   }

   getchar ();

   MHD_stop_daemon (daemon);
   config_destroy (&config_openqm_httpd_server);
   ohs_config_free ();
   return 0;
}
