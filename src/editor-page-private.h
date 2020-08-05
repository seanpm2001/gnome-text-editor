/* editor-page-private.h
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

#pragma once

#include <gtksourceview/gtksource.h>

#include "editor-animation.h"
#include "editor-binding-group.h"
#include "editor-document-private.h"
#include "editor-page.h"
#include "editor-page-settings.h"
#include "editor-path-private.h"
#include "editor-print-operation.h"
#include "editor-search-bar-private.h"
#include "editor-utils-private.h"
#include "editor-window.h"

G_BEGIN_DECLS

struct _EditorPage
{
  GtkBin                   parent_instance;

  EditorDocument          *document;
  EditorPageSettings      *settings;
  EditorBindingGroup      *settings_bindings;

  EditorAnimation         *progress_animation;

  GtkOverlay              *overlay;
  GtkScrolledWindow       *scroller;
  GtkSourceView           *view;
  GtkProgressBar          *progress_bar;
  GtkRevealer             *search_revealer;
  EditorSearchBar         *search_bar;
  GtkInfoBar              *changed_infobar;
  GtkSourceGutterRenderer *lines_renderer;
};

void          _editor_page_class_actions_init   (EditorPageClass     *klass);
void          _editor_page_actions_init         (EditorPage          *self);
EditorWindow *_editor_page_get_window           (EditorPage          *self);
void          _editor_page_save                 (EditorPage          *self);
void          _editor_page_save_as              (EditorPage          *self);
void          _editor_page_raise                (EditorPage          *self);
void          _editor_page_discard_changes      (EditorPage          *self);
void          _editor_page_print                (EditorPage          *self);
void          _editor_page_copy_all             (EditorPage          *self);
void          _editor_page_discard_changes      (EditorPage          *self);
gint          _editor_page_position             (EditorPage          *self);
gchar        *_editor_page_dup_title_no_i18n    (EditorPage          *self);
void          _editor_page_begin_search         (EditorPage          *self);
void          _editor_page_begin_replace        (EditorPage          *self);
void          _editor_page_hide_search          (EditorPage          *self);
void          _editor_page_scroll_to_insert     (EditorPage          *self);
void          _editor_page_move_next_search     (EditorPage          *self);
void          _editor_page_move_previous_search (EditorPage          *self);

G_END_DECLS
