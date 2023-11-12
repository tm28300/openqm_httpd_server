#include <pcre.h>
#include <libconfig.h>
#include <microhttpd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "openqm_httpd_server.h"

int check_folder_pattern (const char* folder_name, size_t folder_length, pcre *pattern_comp)
{
   int prce_status = pcre_exec (pattern_comp, NULL, folder_name, folder_length, 0, 0, NULL, 0);

   if (prce_status < 0) {
      if (prce_status != PCRE_ERROR_NOMATCH) {
         char error_message_detail [52];

         snprintf (error_message_detail, sizeof (error_message_detail), "Object name PCRE match failed with error %d", prce_status);
         abort_message (error_message_detail);
      }
      return false;
   }
   return true;
}

int extract_subroutine_name_from_url (const char *url, struct connection_info_struct *connection_info)
{
   const char* uri_index = url;
   while (*uri_index == '/') {
      ++uri_index;
   }
   if (*uri_index == '\0') {
      abort_message ("Can't access root path");
#ifdef OHS_DEBUG
      printf ("url root path=%s\n", url);
#endif
      return MHD_HTTP_NOT_FOUND;
   }
   struct url_config_struct *base_url_config = first_url_config;
   while (base_url_config != NULL && *uri_index != '\0') {
      // Find next part in url
      const char *uri_folder_end = strstr (uri_index, "/");
      size_t uri_folder_length = uri_folder_end == NULL ? strlen(uri_index) : uri_folder_end - uri_index;

      char *folder_name = strndup (uri_index, uri_folder_length);
#ifdef OHS_DEBUG
      printf ("Analyze folder=%s\n", folder_name);
#endif

      if (folder_name == NULL) {
         abort_message ("Full memory when extract url");
         return MHD_HTTP_INTERNAL_SERVER_ERROR;
      }

      struct url_config_struct *url_config_find;

      for (url_config_find = NULL ; url_config_find == NULL && base_url_config != NULL ; base_url_config = base_url_config->next) {
         if ((base_url_config->path != NULL && strcasecmp (folder_name, base_url_config->path) == 0) ||
               (base_url_config->pattern_comp != NULL && check_folder_pattern (folder_name, uri_folder_length, base_url_config->pattern_comp))) {
            url_config_find = base_url_config;
         }
      }
      if (url_config_find == NULL) {
         char error_message_detail [1024];

         snprintf (error_message_detail, sizeof (error_message_detail), "Folder/file name \"%s\" not found for url \"%s\"", folder_name, url);
         abort_message (error_message_detail);
         free (folder_name);
         return MHD_HTTP_NOT_FOUND;
      }
#ifdef OHS_DEBUG
      printf ("Find url config path=%s, sub_path=%p\n", url_config_find->path, url_config_find->sub_path);
#endif
      free (folder_name);
      uri_index = uri_folder_end;
      if (uri_index != NULL) {
         while (*uri_index == '/') {
            ++uri_index;
         }
      }
      base_url_config = url_config_find->sub_path;
#ifdef OHS_DEBUG
      if (base_url_config == NULL) {
         printf ("Last folder in urls\n");
      }
      else {
         printf ("Url can have sub-folder\n");
      }
#endif

      if (url_config_find->subr != NULL) {
         connection_info->subr = url_config_find->subr;
      }
      connection_info->method_authorized_length = url_config_find->method_length;
      connection_info->method_authorized = url_config_find->method;
      connection_info->get_param_authorized_length = url_config_find->get_param_length;
      connection_info->get_param_authorized = url_config_find->get_param;
   }
   if (uri_index && *uri_index != '\0') {
      char error_message_detail [1024];

      snprintf (error_message_detail, sizeof (error_message_detail), "Url \"%s\" not found", url);
      abort_message (error_message_detail);
      return MHD_HTTP_NOT_FOUND;
   }
   if (connection_info->subr == NULL) {
      char error_message_detail [1024];

      snprintf (error_message_detail, sizeof (error_message_detail), "Url \"%s\" found but without subroutine name", url);
      abort_message (error_message_detail);
      return MHD_HTTP_NOT_FOUND;
   }
   return 0;
}

bool check_method_authorized (const char *method, struct connection_info_struct *connection_info)
{
   if (connection_info->method_authorized_length < 0) {
      // No control
      return true;
   }
   for (int method_index = 0 ; method_index < connection_info->method_authorized_length ; ++method_index) {
      if (strcasecmp (method, connection_info->method_authorized [method_index]) == 0) {
         return true;
      }
   }
   return false;
}

bool check_get_param_authorized (const char *key, struct connection_info_struct *connection_info)
{
   if (connection_info->get_param_authorized_length < 0) {
      // No control
      return true;
   }
   for (int get_param_index = 0 ; get_param_index < connection_info->get_param_authorized_length ; ++get_param_index) {
      if (strcasecmp (key, connection_info->get_param_authorized [get_param_index]) == 0) {
         return true;
      }
   }
   return false;
}
