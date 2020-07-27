/* editor-window.h
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

#include <libhandy-1/handy.h>

#include "editor-types.h"

G_BEGIN_DECLS

#define EDITOR_TYPE_WINDOW (editor_window_get_type())

G_DECLARE_FINAL_TYPE (EditorWindow, editor_window, EDITOR, WINDOW, HdyApplicationWindow)

EditorPage *editor_window_get_visible_page (EditorWindow *self);
void        editor_window_set_visible_page (EditorWindow *self,
                                            EditorPage   *page);
guint       editor_window_get_n_pages      (EditorWindow *self);
EditorPage *editor_window_get_nth_page     (EditorWindow *self,
                                            guint         nth);

G_END_DECLS
