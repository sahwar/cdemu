/*
 *  libMirage: NULL fragment: Fragment object
 *  Copyright (C) 2007 Rok Mandeljc
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "fragment-null.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_FRAGMENT_NULL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FRAGMENT_NULL, MIRAGE_Fragment_NULLPrivate))

typedef struct {   
    /* Fragment info */
    MIRAGE_FragmentInfo *fragment_info;
} MIRAGE_Fragment_NULLPrivate;


/******************************************************************************\
 *                   MIRAGE_Fragment methods implementations                  *
\******************************************************************************/
static gboolean __mirage_fragment_null_get_fragment_info (MIRAGE_Fragment *self, MIRAGE_FragmentInfo **fragment_info, GError **error) {
    MIRAGE_Fragment_NULLPrivate *_priv = MIRAGE_FRAGMENT_NULL_GET_PRIVATE(self);
    *fragment_info = _priv->fragment_info;
    return TRUE;
}

static gboolean __mirage_fragment_null_can_handle_data_format (MIRAGE_Fragment *self, gchar *filename, GError **error) {
    /* NULL fragment needs to be passed "NULL" as a filename */
    if (strcmp(filename, "NULL")) {
        return FALSE;
    }
    
    return TRUE;
}

static gboolean __mirage_fragment_null_use_the_rest_of_file (MIRAGE_Fragment *self, GError **error) {
    /* No file, nothing to use */
    return TRUE;
}

static gboolean __mirage_fragment_null_read_main_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error) {
    /* Nothing to read */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no data in NULL fragment\n", __func__);
    if (length) {
        *length = 0;
    }    
    return TRUE;
}

static gboolean __mirage_fragment_null_read_subchannel_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error) {
    /* Nothing to read */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no data in NULL fragment\n", __func__);
    if (length) {
        *length = 0;
    }
    return TRUE;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_FragmentClass *parent_class = NULL;

static void __mirage_fragment_null_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Fragment_NULL *self = MIRAGE_FRAGMENT_NULL(instance);
    MIRAGE_Fragment_NULLPrivate *_priv = MIRAGE_FRAGMENT_NULL_GET_PRIVATE(self);
    
    /* Create fragment info */
    _priv->fragment_info = mirage_helper_create_fragment_info(
        "FRAGMENT-NULL",
        "NULL Fragment",
        "1.0.0",
        "Rok Mandeljc",
        "MIRAGE_TYPE_FINTERFACE_NULL",
        2, "N/A", NULL
    );
    
    return;
}


static void __mirage_fragment_null_finalize (GObject *obj) {
    MIRAGE_Fragment_NULL *self = MIRAGE_FRAGMENT_NULL(obj);
    MIRAGE_Fragment_NULLPrivate *_priv = MIRAGE_FRAGMENT_NULL_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);

    /* Free fragment info */
    mirage_helper_destroy_fragment_info(_priv->fragment_info);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_fragment_null_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_FragmentClass *class_fragment = MIRAGE_FRAGMENT_CLASS(g_class);
    MIRAGE_Fragment_NULLClass *klass = MIRAGE_FRAGMENT_NULL_CLASS(g_class);

    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Fragment_NULLPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_fragment_null_finalize;
    
    /* Initialize MIRAGE_Fragment methods */
    class_fragment->get_fragment_info = __mirage_fragment_null_get_fragment_info;
    class_fragment->can_handle_data_format = __mirage_fragment_null_can_handle_data_format;
    class_fragment->use_the_rest_of_file = __mirage_fragment_null_use_the_rest_of_file;
    class_fragment->read_main_data = __mirage_fragment_null_read_main_data;
    class_fragment->read_subchannel_data = __mirage_fragment_null_read_subchannel_data;
        
    return;
}

GType mirage_fragment_null_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Fragment_NULLClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_fragment_null_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Fragment_NULL),
            0,      /* n_preallocs */
            __mirage_fragment_null_instance_init    /* instance_init */
        };
        
        static const GInterfaceInfo interface_info = {
            NULL,   /* interface_init */
            NULL,   /* interface_finalize */
            NULL    /* interface_data */
        };
                
        type = g_type_module_register_type(module, MIRAGE_TYPE_FRAGMENT, "MIRAGE_Fragment_NULL", &info, 0);

        g_type_module_add_interface(module, type, MIRAGE_TYPE_FINTERFACE_NULL, &interface_info);
    }
    
    return type;
}
