#include <libconfig.h>
#include <pcre.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openqm_httpd_server.h"

// Declarations

static void print_memory_full ();
static void free_url_config (struct url_config_struct *url_config);
static char *check_openqm_object_name (const char* object_name);
static struct url_config_struct * read_url_config (config_setting_t *config_url_elem);

// Constants

static const char config_file_name [] = "/etc/openqm_httpd_server.cfg";
static const char config_path_openqm_account [] = "openqm.account";
static const char config_path_httpd_port [] = "httpd.port";
static const char pattern_object_name [] = "^[[:alpha:]][[:alnum:]._-]*$";

// Globals variables

config_t config_openqm_httpd_server;
const char *config_openqm_account;
int config_http_port;
struct url_config_struct *first_url_config = NULL;

// Functions

void print_memory_full ()
{
   fprintf (stderr, "Memory full when read configuration\n");
}

void free_url_config (struct url_config_struct *url_config)
{
   if (url_config->pattern_comp != NULL) {
      pcre_free (url_config->pattern_comp);
   }
   if (url_config->method != NULL) {
      free (url_config->method);
   }
   if (url_config->get_param != NULL) {
      free (url_config->get_param);
   }
   struct url_config_struct *sub_path_config = url_config->sub_path;
   while (sub_path_config != NULL) {
      struct url_config_struct *next_config = sub_path_config->next;
      free_url_config (sub_path_config);
      sub_path_config = next_config;
   }
   free (url_config);
}

char *check_openqm_object_name (const char* object_name)
{
   pcre *reg_exp;
   size_t nbcar_on = strlen (object_name);
   int prce_status;
   const char *error;
   int erroffset;
   static const size_t error_message_detail_length;

   reg_exp = pcre_compile (pattern_object_name, 0, &error, &erroffset, NULL) ;
   if (reg_exp == NULL) {
      char *error_message_detail = malloc (error_message_detail_length);

      if (error_message_detail == NULL) {
         return "Memory full";
      }
      snprintf (error_message_detail, error_message_detail_length, "Object name PCRE compilation failed at offset %d: %s", erroffset, error);
      return error_message_detail;
   }
   prce_status = pcre_exec (reg_exp, NULL, object_name, nbcar_on, 0, 0, NULL, 0);
   pcre_free (reg_exp);
   if (prce_status < 0) {
      if (prce_status == PCRE_ERROR_NOMATCH) {
         return "Invalid object name";
      }

      char *error_message_detail = malloc (error_message_detail_length);

      if (error_message_detail == NULL) {
         return "Memory full";
      }
      snprintf (error_message_detail, error_message_detail_length, "Object name PCRE match failed with error %d", prce_status);
      return error_message_detail;
   }
   return 0;
}

struct url_config_struct * read_url_config (config_setting_t *config_url_elem)
{
   struct url_config_struct *new_url_config;

   new_url_config = malloc (sizeof (struct url_config_struct));
   if (new_url_config == NULL) {
      print_memory_full ();
      return NULL;
   }
   new_url_config->path = NULL;
   new_url_config->pattern_comp = NULL;
   new_url_config->subr = NULL;
   new_url_config->method_length = -1;
   new_url_config->method = NULL;
   new_url_config->get_param_length = -1;
   new_url_config->get_param = NULL;
   new_url_config->sub_path = NULL;
   new_url_config->next = NULL;

   bool error_config = false;

   // strings
   const char *pattern_string = NULL;

   config_setting_lookup_string (config_url_elem, "path", &new_url_config->path);
   config_setting_lookup_string (config_url_elem, "pattern", &pattern_string);
   config_setting_lookup_string (config_url_elem, "subr", &new_url_config->subr);


   if ((new_url_config->path == NULL) == (pattern_string == NULL)) {
      fprintf (stderr, "Either path (%s) or pattern (%s) must be present\n", new_url_config->path, pattern_string);
      error_config = true;
   }
#ifdef OHS_DEBUG
   printf ("Find path=%s, pattern=%s\n", new_url_config->path, pattern_string);
#endif

   if (pattern_string != NULL) {
      const char *error;
      int erroffset;

      new_url_config->pattern_comp = pcre_compile (pattern_string, 0, &error, &erroffset, NULL);
      if (new_url_config->pattern_comp == NULL) {
         fprintf (stderr, "PCRE compilation failed for pattern \"%s\" at offset %d: %s\n", pattern_string, erroffset, error);
         error_config = true;
      }
   }

   // Check subr name
   if (new_url_config->subr != NULL) {
      char *subr_name_error_message = check_openqm_object_name (new_url_config->subr);
      if (subr_name_error_message != NULL) {
         fprintf (stderr, "Subroutine name (%s) error: %s\n", new_url_config->subr, subr_name_error_message);
         free (subr_name_error_message);
         error_config = true;
      }
   }

   // method
   config_setting_t *config_url_method = config_setting_get_member (config_url_elem, "method");
   if (config_url_method != NULL) {
      if (config_setting_is_array (config_url_method) == CONFIG_FALSE) {
         fprintf (stderr, "method isn't a array\n");
         error_config = true;
      }
      else {
         new_url_config->method_length = (int) config_setting_length (config_url_method);
         if (new_url_config->method_length) {
            new_url_config->method = malloc (sizeof (const char **) * new_url_config->method_length);
            if (new_url_config->method == NULL) {
               print_memory_full ();
               error_config = true;
            }
            else {
               for (unsigned int method_index = 0 ; method_index < new_url_config->method_length ; ++method_index) {
                  const char *method_elem = config_setting_get_string_elem (config_url_method, method_index);
                  if (method_elem == NULL) {
                     fprintf (stderr, "error reading method %d\n", method_index);
                     error_config = true;
                  }
                  else if (strcasecmp (method_elem, "GET") != 0 && strcasecmp (method_elem, "POST") != 0 && strcasecmp (method_elem, "PUT") != 0 && strcasecmp (method_elem, "PATCH") != 0 && strcasecmp (method_elem, "DELETE")) {
                     fprintf (stderr, "unknown method %d\n", method_index);
                     error_config = true;
                     method_elem = NULL;
                  }
                  new_url_config->method [method_index] = method_elem;
               }
            }
         }
      }
   }

   // get_param
   config_setting_t *config_url_get_param = config_setting_get_member (config_url_elem, "get_param");
   if (config_url_get_param != NULL) {
      if (config_setting_is_array (config_url_get_param) == CONFIG_FALSE) {
         fprintf (stderr, "get_param isn't a array\n");
         error_config = true;
      }
      else {
         new_url_config->get_param_length = (int) config_setting_length (config_url_get_param);
         if (new_url_config->get_param_length) {
            new_url_config->get_param = malloc (sizeof (const char **) * new_url_config->get_param_length);
            if (new_url_config->get_param == NULL) {
               print_memory_full ();
               error_config = true;
            }
            else {
               for (unsigned int get_param_index = 0 ; get_param_index < new_url_config->get_param_length ; ++get_param_index) {
                  const char *get_param_elem = config_setting_get_string_elem (config_url_get_param, get_param_index);
                  if (get_param_elem == NULL) {
                     fprintf (stderr, "error reading get_param %d\n", get_param_index);
                     error_config = true;
                  }
                  new_url_config->get_param [get_param_index] = get_param_elem;
               }
            }
         }
      }
   }

   // sub_path
   config_setting_t *config_url_sub_path = config_setting_get_member (config_url_elem, "sub_path");
   if (config_url_sub_path != NULL) {
      if (config_setting_is_list (config_url_sub_path) == CONFIG_FALSE) {
         fprintf (stderr, "sub_path isn't a list\n");
         error_config = true;
      }
      else {
         unsigned int sub_path_length = config_setting_length (config_url_sub_path);
#ifdef OHS_DEBUG
         printf ("Find %u sub_path\n", sub_path_length);
#endif
         if (sub_path_length) {
            struct url_config_struct *prev_sub_path_config = NULL;
            for (unsigned int sub_path_index = 0 ; sub_path_index < sub_path_length ; ++sub_path_index) {
               config_setting_t *sub_path_elem = config_setting_get_elem (config_url_sub_path, sub_path_index);
               struct url_config_struct *sub_path_config = NULL;

               if (sub_path_elem == NULL) {
                  fprintf (stderr, "error reading sub_path %d\n", sub_path_index);
                  error_config = true;
               }
               else if (config_setting_is_group (sub_path_elem) == CONFIG_FALSE) {
                  fprintf (stderr, "sub_path %d isn't a group\n", sub_path_index);
                  error_config = true;
               }
               else {
                  sub_path_config = read_url_config (sub_path_elem);
                  if (sub_path_config == NULL ) {
                     fprintf (stderr, "Previous error in sub_path %d\n", sub_path_index);
                     error_config = true;
                  }
               }
               if (sub_path_config != NULL) {
                  if (prev_sub_path_config == NULL) {
                     new_url_config->sub_path = sub_path_config;
                  }
                  else {
                     prev_sub_path_config->next = sub_path_config;
                  }
                  prev_sub_path_config = sub_path_config;
               }
            }
#ifdef OHS_DEBUG
            printf ("Config->sub_path=%p\n", new_url_config->sub_path);
#endif
         }
      }
   }

   if (error_config) {
      free_url_config (new_url_config);
      new_url_config = NULL;
   }

   return new_url_config;
}

bool ohs_config_read ()
{
   if (config_read_file (&config_openqm_httpd_server, config_file_name) != CONFIG_TRUE) {
      fprintf (stderr, "Can't read configuration file %s:%d %s\n", config_file_name, config_error_line (&config_openqm_httpd_server), config_error_text (&config_openqm_httpd_server));
      return false;
   }
   // httpd.port
   if (config_lookup_int (&config_openqm_httpd_server, config_path_httpd_port, &config_http_port) != CONFIG_TRUE) {
      fprintf (stderr, "Can't find configuration %s in file %s:%d %s\n", config_path_httpd_port, config_error_file (&config_openqm_httpd_server), config_error_line (&config_openqm_httpd_server), config_error_text (&config_openqm_httpd_server));
      return false;
   }
   // httpd.env
   config_setting_t *config_httpd_env = config_lookup (&config_openqm_httpd_server, "httpd.env");
   if (config_httpd_env != NULL) {
      unsigned int env_count = config_setting_length (config_httpd_env);

      if (config_setting_is_group (config_httpd_env) == CONFIG_FALSE) {
         fprintf (stderr, "Incorrect url configuration type\n");
         return false;
      }
#ifdef OHS_DEBUG
      printf ("Config httpd.env=%u\n", env_count);
#endif
      for (unsigned int env_index = 0 ; env_index < env_count ; ++env_index) {
         config_setting_t *config_httpd_env_index = config_setting_get_elem (config_httpd_env, env_index);
         if (config_httpd_env_index != NULL) {
            const char *name = config_setting_name (config_httpd_env_index);
            const char *value = config_setting_get_string (config_httpd_env_index);
            if (name != NULL && value != NULL) {
               setenv (name, value, true);
#ifdef OHS_DEBUG
               printf ("Setenv %s=%s\n", name, value);
#endif
            }
         }
      }
   }
   // openqm.account
   if (config_lookup_string (&config_openqm_httpd_server, config_path_openqm_account, &config_openqm_account) != CONFIG_TRUE) {
      fprintf (stderr, "Can't find configuration %s in file %s:%d %s\n", config_path_openqm_account, config_error_file (&config_openqm_httpd_server), config_error_line (&config_openqm_httpd_server), config_error_text (&config_openqm_httpd_server));
      return false;
   }
   if (!strlen (config_path_openqm_account)) {
      fprintf (stderr, "OpenQM account not configured\n");
      return false;
   }

   // url
   config_setting_t *config_url = config_lookup (&config_openqm_httpd_server, "url");
   if (config_url == NULL) {
      fprintf (stderr, "Missing url configuration\n");
      return false;
   }
   if (config_setting_is_list (config_url) == CONFIG_FALSE) {
      fprintf (stderr, "url isn't a list\n");
      return false;
   }

   unsigned int url_length = config_setting_length (config_url);
   for (unsigned int url_index = 0 ; url_index < url_length ; ++url_index) {
      config_setting_t *config_url_elem = config_setting_get_elem (config_url, url_index);
      if (config_url_elem != NULL) {
         struct url_config_struct *new_url_config = read_url_config (config_url_elem);
         if (new_url_config == NULL) {
            fprintf (stderr, "Previous error in url %d\n", url_index);
            return false;
         }
         new_url_config->next = first_url_config;
         first_url_config = new_url_config;
      }
   }
#ifdef OHS_DEBUG
   printf ("First config path=%s sub_path=%p\n", first_url_config->path, first_url_config->sub_path);
#endif

   return true;
}

void ohs_config_free ()
{
   while (first_url_config != NULL) {
      struct url_config_struct *current_url_config = first_url_config;

      first_url_config = current_url_config->next;
      free_url_config (current_url_config);
   }
}
