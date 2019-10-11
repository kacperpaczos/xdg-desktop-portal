#include <config.h>
#include <stdio.h>

#include <gio/gio.h>

#include "src/xdp-impl-dbus.h"

#define BACKEND_BUS_NAME "org.freedesktop.impl.portal.Test"
#define BACKEND_OBJECT_PATH "/org/freedesktop/portal/desktop"

static GMainLoop *loop;

static gboolean
handle_get_user_information (XdpImplAccount *object,
                             GDBusMethodInvocation *invocation,
                             const char *arg_handle,
                             const char *arg_app_id,
                             const char *arg_parent_window,
                             GVariant *arg_options)
{
  GVariantBuilder opt_builder;
  const char *dir;
  g_autofree char *path = NULL;
  g_autoptr(GKeyFile) keyfile = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *reason = NULL;
  g_autofree char *id = NULL;
  g_autofree char *name = NULL;
  g_autofree char *image = NULL;
  const char *r;

  dir = g_getenv ("XDG_DATA_HOME");
  path = g_build_filename (dir, "account", NULL);

  keyfile = g_key_file_new ();
  g_key_file_load_from_file (keyfile, path, 0, &error);
  g_assert_no_error (error);

  reason = g_key_file_get_string (keyfile, "account", "reason", &error);
  id = g_key_file_get_string (keyfile, "account", "id", &error);
  name = g_key_file_get_string (keyfile, "account", "name", &error);
  image = g_key_file_get_string (keyfile, "account", "image", &error);

  g_variant_lookup (arg_options, "reason", "&s", &r);
  if (g_strcmp0 (r, reason) != 0)
    {
      g_dbus_method_invocation_return_error (invocation, G_IO_ERROR, G_IO_ERROR_FAILED, "Bad reason");
      return TRUE;
    }

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  if (id)
    g_variant_builder_add (&opt_builder, "{sv}", "id", g_variant_new_string (id));
  if (name)
    g_variant_builder_add (&opt_builder, "{sv}", "name", g_variant_new_string (name));
  if (image)
    g_variant_builder_add (&opt_builder, "{sv}", "image", g_variant_new_string (image));

  xdp_impl_account_complete_get_user_information (object,
                                                  invocation,
                                                  0,
                                                  g_variant_builder_end (&opt_builder));

  return TRUE;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  g_autoptr(GError) error = NULL;
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_account_skeleton_new ());

  g_signal_connect (helper, "handle-get-user-information", G_CALLBACK (handle_get_user_information), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         connection,
                                         BACKEND_OBJECT_PATH,
                                         &error))
    {
      g_printerr ("Failed to export %s skeleton: %s\n",
                  g_dbus_interface_skeleton_get_info (helper)->name,
                  error->message);
      exit (1);
    }

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  g_debug ("%s acquired", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  g_main_loop_quit (loop);
}

static gboolean opt_verbose;
static gboolean opt_replace;

static GOptionEntry entries[] = {
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Print debug information during command processing", NULL },
  { "replace", 'r', 0, G_OPTION_ARG_NONE, &opt_replace, "Replace a running instance", NULL },
  { NULL }
};

static void
message_handler (const char *log_domain,
                 GLogLevelFlags log_level,
                 const char *message,
                 gpointer user_data)
{
  if (log_level & G_LOG_LEVEL_DEBUG)
    printf ("TST: %s\n", message);
  else
    printf ("%s: %s\n", g_get_prgname (), message);
}

static void
printerr_handler (const gchar *string)
{
  int is_tty = isatty (1);
  const char *prefix = "";
  const char *suffix = "";
  if (is_tty)
    {
      prefix = "\x1b[31m\x1b[1m"; /* red, bold */
      suffix = "\x1b[22m\x1b[0m"; /* bold off, color reset */
    }
  fprintf (stderr, "%serror: %s%s\n", prefix, suffix, string);
}

int
main (int argc, char *argv[])
{
  guint owner_id;
  g_autoptr(GError) error = NULL;
  g_autoptr(GDBusConnection) session_bus = NULL;
  g_autoptr(GOptionContext) context = NULL;

  g_setenv ("GIO_USE_VFS", "local", TRUE);

  g_set_prgname (argv[0]);

  context = g_option_context_new ("- portal test backends");
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s: %s", g_get_application_name (), error->message);
      g_printerr ("\n");
      return 1;
    }

  g_set_printerr_handler (printerr_handler);
  if (opt_verbose)
    g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, message_handler, NULL);

  loop = g_main_loop_new (NULL, FALSE);

  session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (session_bus == NULL)
    {
      g_printerr ("No session bus: %s", error->message);
      return 2;
    }

  owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                             BACKEND_BUS_NAME,
                             G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT | (opt_replace ? G_BUS_NAME_OWNER_FLAGS_REPLACE : 0),
                             on_bus_acquired,
                             on_name_acquired,
                             on_name_lost,
                             NULL,
                             NULL);

  g_main_loop_run (loop);

  g_bus_unown_name (owner_id);
  g_main_loop_unref (loop);

  return 0;
}
