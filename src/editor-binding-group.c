/* editor-binding-group.c
 *
 * Copyright (C) 2015 Christian Hergert <christian@hergert.me>
 * Copyright (C) 2015 Garrett Regier <garrettregier@gmail.com>
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define G_LOG_DOMAIN "editor-binding-group"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-binding-group.h"

/**
 * SECTION:editor-binding-group:
 * @title: EditorBindingGroup
 * @short_description: Binding multiple properties as a group
 *
 * The #EditorBindingGroup can be used to bind multiple properties
 * from an object collectively.
 *
 * Use the various methods to bind properties from a single source
 * object to multiple destination objects. Properties can be bound
 * bidrectionally and are connected when the source object is set
 * with editor_binding_group_set_source().
 */

struct _EditorBindingGroup
{
  GObject    parent_instance;
  GObject   *source;
  GPtrArray *lazy_bindings;
};

typedef struct
{
  EditorBindingGroup *group;
  const char         *source_property;
  const char         *target_property;
  GObject            *target;
  GBinding           *binding;
  gpointer            user_data;
  GDestroyNotify      user_data_destroy;
  gpointer            transform_to;
  gpointer            transform_from;
  GBindingFlags       binding_flags;
  guint               using_closures : 1;
} LazyBinding;

G_DEFINE_TYPE (EditorBindingGroup, editor_binding_group, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_SOURCE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/*#define DEBUG_BINDINGS 1*/

#ifdef DEBUG_BINDINGS
static char *
_g_flags_to_string (GFlagsClass *flags_class,
                    guint        value)
{
  GString *str;
  GFlagsValue *flags_value;
  gboolean first = TRUE;

  str = g_string_new (NULL);

  while ((first || value != 0) &&
         (flags_value = g_flags_get_first_value (flags_class, value)) != NULL)
    {
      if (!first)
        g_string_append (str, " | ");

      g_string_append (str, flags_value->value_name);

      first = FALSE;
      value &= ~(flags_value->value);
    }

  return g_string_free (str, FALSE);
}
#endif

static void
editor_binding_group_connect (EditorBindingGroup *self,
                              LazyBinding        *lazy_binding)
{
  GBinding *binding;

  g_assert (EDITOR_IS_BINDING_GROUP (self));
  g_assert (self->source != NULL);
  g_assert (lazy_binding != NULL);
  g_assert (lazy_binding->binding == NULL);
  g_assert (lazy_binding->target != NULL);
  g_assert (lazy_binding->target_property != NULL);
  g_assert (lazy_binding->source_property != NULL);

#ifdef DEBUG_BINDINGS
  {
    GFlagsClass *flags_class;
    g_autofree gchar *flags_str = NULL;

    flags_class = g_type_class_ref (G_TYPE_BINDING_FLAGS);
    flags_str = _g_flags_to_string (flags_class,
                                    lazy_binding->binding_flags);

    g_print ("Binding %s(%p):%s to %s(%p):%s (flags=%s)\n",
             G_OBJECT_TYPE_NAME (self->source),
             self->source,
             lazy_binding->source_property,
             G_OBJECT_TYPE_NAME (lazy_binding->target),
             lazy_binding->target,
             lazy_binding->target_property,
             flags_str);

    g_type_class_unref (flags_class);
  }
#endif

  if (!lazy_binding->using_closures)
    binding = g_object_bind_property_full (self->source,
                                           lazy_binding->source_property,
                                           lazy_binding->target,
                                           lazy_binding->target_property,
                                           lazy_binding->binding_flags,
                                           lazy_binding->transform_to,
                                           lazy_binding->transform_from,
                                           lazy_binding->user_data,
                                           NULL);
  else
    binding = g_object_bind_property_with_closures (self->source,
                                                    lazy_binding->source_property,
                                                    lazy_binding->target,
                                                    lazy_binding->target_property,
                                                    lazy_binding->binding_flags,
                                                    lazy_binding->transform_to,
                                                    lazy_binding->transform_from);

  lazy_binding->binding = binding;
}

static void
editor_binding_group_disconnect (LazyBinding *lazy_binding)
{
  g_assert (lazy_binding != NULL);

  if (lazy_binding->binding != NULL)
    {
      g_binding_unbind (lazy_binding->binding);
      lazy_binding->binding = NULL;
    }
}

static void
editor_binding_group__source_weak_notify (gpointer  data,
                                          GObject  *where_object_was)
{
  EditorBindingGroup *self = data;
  gsize i;

  g_assert (EDITOR_IS_BINDING_GROUP (self));

  self->source = NULL;

  for (i = 0; i < self->lazy_bindings->len; i++)
    {
      LazyBinding *lazy_binding;

      lazy_binding = g_ptr_array_index (self->lazy_bindings, i);
      lazy_binding->binding = NULL;
    }
}

static void
editor_binding_group__target_weak_notify (gpointer  data,
                                          GObject  *where_object_was)
{
  EditorBindingGroup *self = data;
  gsize i;

  g_assert (EDITOR_IS_BINDING_GROUP (self));

  for (i = 0; i < self->lazy_bindings->len; i++)
    {
      LazyBinding *lazy_binding;

      lazy_binding = g_ptr_array_index (self->lazy_bindings, i);

      if (lazy_binding->target == where_object_was)
        {
          lazy_binding->target = NULL;
          lazy_binding->binding = NULL;

          g_ptr_array_remove_index_fast (self->lazy_bindings, i);
          break;
        }
    }
}

static void
lazy_binding_free (gpointer data)
{
  LazyBinding *lazy_binding = data;

  if (lazy_binding->target != NULL)
    {
      g_object_weak_unref (lazy_binding->target,
                           editor_binding_group__target_weak_notify,
                           lazy_binding->group);
      lazy_binding->target = NULL;
    }

  editor_binding_group_disconnect (lazy_binding);

  lazy_binding->group = NULL;
  lazy_binding->source_property = NULL;
  lazy_binding->target_property = NULL;

  if (lazy_binding->user_data_destroy)
    lazy_binding->user_data_destroy (lazy_binding->user_data);

  if (lazy_binding->using_closures)
    {
      g_clear_pointer (&lazy_binding->transform_to, g_closure_unref);
      g_clear_pointer (&lazy_binding->transform_from, g_closure_unref);
    }

  g_slice_free (LazyBinding, lazy_binding);
}

static void
editor_binding_group_dispose (GObject *object)
{
  EditorBindingGroup *self = (EditorBindingGroup *)object;

  g_assert (EDITOR_IS_BINDING_GROUP (self));

  if (self->source != NULL)
    {
      g_object_weak_unref (self->source,
                           editor_binding_group__source_weak_notify,
                           self);
      self->source = NULL;
    }

  if (self->lazy_bindings->len != 0)
    g_ptr_array_remove_range (self->lazy_bindings, 0, self->lazy_bindings->len);

  G_OBJECT_CLASS (editor_binding_group_parent_class)->dispose (object);
}

static void
editor_binding_group_finalize (GObject *object)
{
  EditorBindingGroup *self = (EditorBindingGroup *)object;

  g_assert (self->lazy_bindings != NULL);
  g_assert (self->lazy_bindings->len == 0);

  g_clear_pointer (&self->lazy_bindings, g_ptr_array_unref);

  G_OBJECT_CLASS (editor_binding_group_parent_class)->finalize (object);
}

static void
editor_binding_group_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  EditorBindingGroup *self = EDITOR_BINDING_GROUP (object);

  switch (prop_id)
    {
    case PROP_SOURCE:
      g_value_set_object (value, editor_binding_group_get_source (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_binding_group_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  EditorBindingGroup *self = EDITOR_BINDING_GROUP (object);

  switch (prop_id)
    {
    case PROP_SOURCE:
      editor_binding_group_set_source (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_binding_group_class_init (EditorBindingGroupClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = editor_binding_group_dispose;
  object_class->finalize = editor_binding_group_finalize;
  object_class->get_property = editor_binding_group_get_property;
  object_class->set_property = editor_binding_group_set_property;

  /**
   * EditorBindingGroup:source:
   *
   * The source object used for binding properties.
   */
  properties [PROP_SOURCE] =
    g_param_spec_object ("source",
                         "Source",
                         "The source GObject used for binding properties.",
                         G_TYPE_OBJECT,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_binding_group_init (EditorBindingGroup *self)
{
  self->lazy_bindings = g_ptr_array_new_with_free_func (lazy_binding_free);
}

/**
 * editor_binding_group_new:
 *
 * Creates a new #EditorBindingGroup.
 *
 * Returns: a new #EditorBindingGroup
 */
EditorBindingGroup *
editor_binding_group_new (void)
{
  return g_object_new (EDITOR_TYPE_BINDING_GROUP, NULL);
}

/**
 * editor_binding_group_get_source:
 * @self: the #EditorBindingGroup
 *
 * Gets the source object used for binding properties.
 *
 * Returns: (transfer none) (nullable): the source object.
 */
GObject *
editor_binding_group_get_source (EditorBindingGroup *self)
{
  g_return_val_if_fail (EDITOR_IS_BINDING_GROUP (self), NULL);

  return self->source;
}

static gboolean
editor_binding_group_check_source (EditorBindingGroup *self,
                                   gpointer            source)
{
  gsize i;

  for (i = 0; i < self->lazy_bindings->len; i++)
    {
      LazyBinding *lazy_binding = g_ptr_array_index (self->lazy_bindings, i);

      g_return_val_if_fail (g_object_class_find_property (G_OBJECT_GET_CLASS (source),
                                                          lazy_binding->source_property) != NULL,
                            FALSE);
    }

  return TRUE;
}

/**
 * editor_binding_group_set_source:
 * @self: the #EditorBindingGroup
 * @source: (type GObject) (nullable): the source #GObject
 *
 * Sets @source as the source object used for creating property
 * bindings. If there is already a source object all bindings from it
 * will be removed.
 *
 * Note: All properties that have been bound must exist on @source.
 */
void
editor_binding_group_set_source (EditorBindingGroup *self,
                                 gpointer            source)
{
  g_return_if_fail (EDITOR_IS_BINDING_GROUP (self));
  g_return_if_fail (!source || G_IS_OBJECT (source));
  g_return_if_fail (source != (gpointer)self);

  if (source == (gpointer)self->source)
    return;

  if (self->source != NULL)
    {
      gsize i;

      g_object_weak_unref (self->source,
                           editor_binding_group__source_weak_notify,
                           self);
      self->source = NULL;

      for (i = 0; i < self->lazy_bindings->len; i++)
        {
          LazyBinding *lazy_binding;

          lazy_binding = g_ptr_array_index (self->lazy_bindings, i);
          editor_binding_group_disconnect (lazy_binding);
        }
    }

  if (source != NULL && editor_binding_group_check_source (self, source))
    {
      gsize i;

      self->source = source;
      g_object_weak_ref (self->source,
                         editor_binding_group__source_weak_notify,
                         self);

      for (i = 0; i < self->lazy_bindings->len; i++)
        {
          LazyBinding *lazy_binding;

          lazy_binding = g_ptr_array_index (self->lazy_bindings, i);
          editor_binding_group_connect (self, lazy_binding);
        }
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SOURCE]);
}

static void
editor_binding_group_bind_helper (EditorBindingGroup *self,
                                  const gchar        *source_property,
                                  gpointer            target,
                                  const gchar        *target_property,
                                  GBindingFlags       flags,
                                  gpointer            transform_to,
                                  gpointer            transform_from,
                                  gpointer            user_data,
                                  GDestroyNotify      user_data_destroy,
                                  gboolean            using_closures)
{
  LazyBinding *lazy_binding;

  g_return_if_fail (EDITOR_IS_BINDING_GROUP (self));
  g_return_if_fail (source_property != NULL);
  g_return_if_fail (self->source == NULL ||
                    g_object_class_find_property (G_OBJECT_GET_CLASS (self->source),
                                                  source_property) != NULL);
  g_return_if_fail (G_IS_OBJECT (target));
  g_return_if_fail (target_property != NULL);
  g_return_if_fail (g_object_class_find_property (G_OBJECT_GET_CLASS (target),
                                                  target_property) != NULL);
  g_return_if_fail (target != (gpointer)self ||
                    strcmp (source_property, target_property) != 0);

  lazy_binding = g_slice_new0 (LazyBinding);
  lazy_binding->group = self;
  lazy_binding->source_property = g_intern_string (source_property);
  lazy_binding->target_property = g_intern_string (target_property);
  lazy_binding->target = target;
  lazy_binding->binding_flags = flags | G_BINDING_SYNC_CREATE;
  lazy_binding->user_data = user_data;
  lazy_binding->user_data_destroy = user_data_destroy;
  lazy_binding->transform_to = transform_to;
  lazy_binding->transform_from = transform_from;

  if (using_closures)
    {
      lazy_binding->using_closures = TRUE;

      if (transform_to != NULL)
        g_closure_sink (g_closure_ref (transform_to));

      if (transform_from != NULL)
        g_closure_sink (g_closure_ref (transform_from));
    }

  g_object_weak_ref (target,
                     editor_binding_group__target_weak_notify,
                     self);

  g_ptr_array_add (self->lazy_bindings, lazy_binding);

  if (self->source != NULL)
    editor_binding_group_connect (self, lazy_binding);
}

/**
 * editor_binding_group_bind:
 * @self: the #EditorBindingGroup
 * @source_property: the property on the source to bind
 * @target: (type GObject): the target #GObject
 * @target_property: the property on @target to bind
 * @flags: the flags used to create the #GBinding
 *
 * Creates a binding between @source_property on the source object
 * and @target_property on @target. Whenever the @source_property
 * is changed the @target_property is updated using the same value.
 * The binding flags #G_BINDING_SYNC_CREATE is automatically specified.
 *
 * See: g_object_bind_property().
 */
void
editor_binding_group_bind (EditorBindingGroup *self,
                           const gchar        *source_property,
                           gpointer            target,
                           const gchar        *target_property,
                           GBindingFlags       flags)
{
  editor_binding_group_bind_full (self, source_property,
                                  target, target_property,
                                  flags,
                                  NULL, NULL,
                                  NULL, NULL);
}

/**
 * editor_binding_group_bind_full:
 * @self: the #EditorBindingGroup
 * @source_property: the property on the source to bind
 * @target: (type GObject): the target #GObject
 * @target_property: the property on @target to bind
 * @flags: the flags used to create the #GBinding
 * @transform_to: (scope notified) (nullable): the transformation function
 *     from the source object to the @target, or %NULL to use the default
 * @transform_from: (scope notified) (nullable): the transformation function
 *     from the @target to the source object, or %NULL to use the default
 * @user_data: custom data to be passed to the transformation
 *             functions, or %NULL
 * @user_data_destroy: function to be called when disposing the binding,
 *     to free the resources used by the transformation functions
 *
 * Creates a binding between @source_property on the source object and
 * @target_property on @target, allowing you to set the transformation
 * functions to be used by the binding. The binding flags
 * #G_BINDING_SYNC_CREATE is automatically specified.
 *
 * See: g_object_bind_property_full().
 */
void
editor_binding_group_bind_full (EditorBindingGroup    *self,
                                const gchar           *source_property,
                                gpointer               target,
                                const gchar           *target_property,
                                GBindingFlags          flags,
                                GBindingTransformFunc  transform_to,
                                GBindingTransformFunc  transform_from,
                                gpointer               user_data,
                                GDestroyNotify         user_data_destroy)
{
  editor_binding_group_bind_helper (self, source_property,
                                    target, target_property,
                                    flags,
                                    transform_to, transform_from,
                                    user_data, user_data_destroy,
                                    FALSE);
}

/**
 * editor_binding_group_bind_with_closures: (rename-to editor_binding_group_bind_full)
 * @self: the #EditorBindingGroup
 * @source_property: the property on the source to bind
 * @target: (type GObject): the target #GObject
 * @target_property: the property on @target to bind
 * @flags: the flags used to create the #GBinding
 * @transform_to: (nullable): a #GClosure wrapping the
 *     transformation function from the source object to the @target,
 *     or %NULL to use the default
 * @transform_from: (nullable): a #GClosure wrapping the
 *     transformation function from the @target to the source object,
 *     or %NULL to use the default
 *
 * Creates a binding between @source_property on the source object and
 * @target_property on @target, allowing you to set the transformation
 * functions to be used by the binding. The binding flags
 * #G_BINDING_SYNC_CREATE is automatically specified.
 *
 * This function is the language bindings friendly version of
 * editor_binding_group_bind_property_full(), using #GClosures
 * instead of function pointers.
 *
 * See: g_object_bind_property_with_closures().
 */
void
editor_binding_group_bind_with_closures (EditorBindingGroup *self,
                                         const gchar        *source_property,
                                         gpointer            target,
                                         const gchar        *target_property,
                                         GBindingFlags       flags,
                                         GClosure           *transform_to,
                                         GClosure           *transform_from)
{
  editor_binding_group_bind_helper (self, source_property,
                                    target, target_property,
                                    flags,
                                    transform_to, transform_from,
                                    NULL, NULL,
                                    TRUE);
}
