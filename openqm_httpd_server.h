/*
 */

#define OHS_DEBUG 1

// Types

enum connection_type_enum {
   ct_post,
   ct_get
};

struct post_info_struct
{
   enum connection_type_enum connection_type;
   char *post_dynarray;
   bool error_status;
   struct MHD_PostProcessor *post_processor; 
};

struct url_config_struct {
   const char  *path;
   pcre        *pattern_comp;
   const char  *subr;
   int          method_length;
   const char **method;
   int          get_param_length;
   const char **get_param;
   struct url_config_struct *sub_path;
   struct url_config_struct *next;
};

struct connection_info_struct {
   struct post_info_struct *post_info;
   const char              *subr;
   int                      method_authorized_length;
   const char             **method_authorized;
   int                      get_param_authorized_length;
   const char             **get_param_authorized;
};

// Globals variables

extern config_t config_openqm_httpd_server;
extern const char *config_openqm_account;
extern int config_http_port;
extern struct url_config_struct *first_url_config;

// Globals functions

extern void abort_message (const char *error_message);
extern bool ohs_config_read ();
extern void ohs_config_free ();
extern int extract_subroutine_name_from_url (const char *url, struct connection_info_struct *connection_info);
extern bool check_method_authorized (const char *method, struct connection_info_struct *connection_info);
extern bool check_get_param_authorized (const char *key, struct connection_info_struct *connection_info);
