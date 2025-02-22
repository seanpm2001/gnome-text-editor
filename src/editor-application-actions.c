/* editor-application-actions.c
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "editor-application-actions"

#include "build-ident.h"
#include "config.h"

#include <glib/gi18n.h>

#include "editor-application-private.h"
#include "editor-page.h"
#include "editor-save-changes-dialog-private.h"
#include "editor-session-private.h"
#include "editor-window.h"

static const gchar *authors[] = {
  "Christian Hergert",
  "Christopher Davis",
  NULL
};

static const gchar *artists[] = {
  "Allan Day",
  "Jakub Steiner",
  "Tobias Bernard",
  NULL
};

static void
editor_application_actions_new_window_cb (GSimpleAction *action,
                                          GVariant      *param,
                                          gpointer       user_data)
{
  EditorApplication *self = user_data;
  EditorSession *session;
  EditorWindow *current;

  g_assert (EDITOR_IS_APPLICATION (self));

  current = editor_application_get_current_window (self);
  session = editor_application_get_session (self);

  if (current != NULL && !editor_window_get_visible_page (current))
    editor_session_add_draft (session, current);
  else
    editor_session_create_window (session);
}

static void
editor_application_actions_about_cb (GSimpleAction *action,
                                     GVariant      *param,
                                     gpointer       user_data)
{
  EditorApplication *self = user_data;
  g_autofree gchar *program_name = NULL;
  GtkAboutDialog *dialog;
  EditorWindow *window;

  g_assert (EDITOR_IS_APPLICATION (self));

#if DEVELOPMENT_BUILD
  program_name = g_strdup_printf ("%s (Development)", _("Text Editor"));
#else
  program_name = g_strdup (_("Text Editor"));
#endif

  dialog = GTK_ABOUT_DIALOG (gtk_about_dialog_new ());
  gtk_about_dialog_set_program_name (dialog, program_name);
  gtk_about_dialog_set_logo_icon_name (dialog, PACKAGE_ICON_NAME);
  gtk_about_dialog_set_authors (dialog, authors);
  gtk_about_dialog_set_artists (dialog, artists);
#if DEVELOPMENT_BUILD
  gtk_about_dialog_set_version (dialog, PACKAGE_VERSION" (" EDITOR_BUILD_IDENTIFIER ")");
  gtk_widget_add_css_class (GTK_WIDGET (dialog), "devel");
#else
  gtk_about_dialog_set_version (dialog, PACKAGE_VERSION);
#endif
  gtk_about_dialog_set_copyright (dialog, "© 2020-2021 Christian Hergert");
  gtk_about_dialog_set_license_type (dialog, GTK_LICENSE_GPL_3_0);
  gtk_about_dialog_set_website (dialog, PACKAGE_WEBSITE);
  gtk_about_dialog_set_website_label (dialog, _("Text Editor Website"));

  window = editor_application_get_current_window (self);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

  gtk_window_present (GTK_WINDOW (dialog));
}

static void
editor_application_actions_help_cb (GSimpleAction *action,
                                    GVariant      *param,
                                    gpointer       user_data)
{
  EditorApplication *self = user_data;
  EditorWindow *window;

  g_assert (EDITOR_IS_APPLICATION (self));

  window = editor_application_get_current_window (self);

  gtk_show_uri (GTK_WINDOW (window), "help:gnome-text-editor", GDK_CURRENT_TIME);
}

static void
editor_application_actions_quit_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  EditorSession *session = (EditorSession *)object;
  g_autoptr(EditorApplication) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (EDITOR_IS_APPLICATION (self));

  if (!editor_session_save_finish (session, result, &error))
    {
      g_warning ("Failed to save session: %s", error->message);
      return;
    }

  g_application_quit (G_APPLICATION (self));
}

static void
editor_application_actions_confirm_cb (GObject      *object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  g_autoptr(EditorApplication) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (EDITOR_IS_APPLICATION (self));

  if (!_editor_save_changes_dialog_run_finish (result, &error))
    return;

  editor_session_save_async (self->session,
                             NULL,
                             editor_application_actions_quit_cb,
                             g_object_ref (self));
}

static void
editor_application_actions_quit (GSimpleAction *action,
                                 GVariant      *param,
                                 gpointer       user_data)
{
  EditorApplication *self = user_data;
  g_autoptr(GPtrArray) unsaved = NULL;

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (EDITOR_IS_APPLICATION (self));

  unsaved = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < self->session->pages->len; i++)
    {
      EditorPage *page = g_ptr_array_index (self->session->pages, i);

      if (editor_page_get_is_modified (page))
        g_ptr_array_add (unsaved, g_object_ref (page));
    }

  if (unsaved->len > 0)
    {
      _editor_save_changes_dialog_run_async (gtk_application_get_active_window (GTK_APPLICATION (self)),
                                             unsaved,
                                             NULL,
                                             editor_application_actions_confirm_cb,
                                             g_object_ref (self));
      return;
    }

  editor_session_save_async (self->session,
                             NULL,
                             editor_application_actions_quit_cb,
                             g_object_ref (self));
}

static void
editor_application_actions_remove_recent_cb (GSimpleAction *action,
                                             GVariant      *param,
                                             gpointer       user_data)
{
  g_autoptr(GFile) file = NULL;
  const char *uri;
  const char *draft_id;

  g_assert (EDITOR_IS_APPLICATION (user_data));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE ("(ss)")));

  g_variant_get (param, "(&s&s)", &uri, &draft_id);

  if (uri[0] != 0)
    file = g_file_new_for_uri (uri);

  if (draft_id[0] == 0)
    draft_id = NULL;

  _editor_session_forget (EDITOR_SESSION_DEFAULT, file, draft_id);
}

void
_editor_application_actions_init (EditorApplication *self)
{
  static const GActionEntry actions[] = {
    { "new-window", editor_application_actions_new_window_cb },
    { "about", editor_application_actions_about_cb },
    { "help", editor_application_actions_help_cb },
    { "quit", editor_application_actions_quit },
    { "remove-recent", editor_application_actions_remove_recent_cb, "(ss)" },
  };
  g_autoptr(GPropertyAction) style_scheme = NULL;

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   actions,
                                   G_N_ELEMENTS (actions),
                                   self);

  style_scheme = g_property_action_new ("style-scheme", self, "style-scheme");
  g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (style_scheme));
}
