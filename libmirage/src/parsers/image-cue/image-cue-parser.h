/*
 *  libMirage: CUE image parser: Parser object
 *  Copyright (C) 2006-2008 Rok Mandeljc
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

#ifndef __IMAGE_CUE_PARSER_H__
#define __IMAGE_CUE_PARSER_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_PARSER_CUE            (mirage_parser_cue_get_type(global_module))
#define MIRAGE_PARSER_CUE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_PARSER_CUE, MIRAGE_Parser_CUE))
#define MIRAGE_PARSER_CUE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_PARSER_CUE, MIRAGE_Parser_CUEClass))
#define MIRAGE_IS_PARSER_CUE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_PARSER_CUE))
#define MIRAGE_IS_PARSER_CUE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_PARSER_CUE))
#define MIRAGE_PARSER_CUE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_PARSER_CUE, MIRAGE_Parser_CUEClass))

typedef struct {
    MIRAGE_Parser parent;
} MIRAGE_Parser_CUE;

typedef struct {
    MIRAGE_ParserClass parent;
} MIRAGE_Parser_CUEClass;

/* Used by MIRAGE_TYPE_PARSER_CUE */
GType mirage_parser_cue_get_type (GTypeModule *module);

/* Helpers */
gboolean __mirage_parser_cue_finish_last_track (MIRAGE_Parser *self, GError **error);
gboolean __mirage_parser_cue_set_cue_filename (MIRAGE_Parser *self, gchar *filename, GError **error);
gboolean __mirage_parser_cue_set_mcn (MIRAGE_Parser *self, gchar *mcn, GError **error);
gboolean __mirage_parser_cue_set_new_file (MIRAGE_Parser *self, gchar *filename_string, gchar *file_type, GError **error);
gboolean __mirage_parser_cue_add_track (MIRAGE_Parser *self, gint number, gchar *mode_string, GError **error);
gboolean __mirage_parser_cue_add_index (MIRAGE_Parser *self, gint number, gint address, GError **error);
gboolean __mirage_parser_cue_set_flag (MIRAGE_Parser *self, gint flag, GError **error);
gboolean __mirage_parser_cue_set_isrc (MIRAGE_Parser *self, gchar *isrc, GError **error);
gboolean __mirage_parser_cue_add_pregap (MIRAGE_Parser *self, gint length, GError **error);
gboolean __mirage_parser_cue_add_empty_part (MIRAGE_Parser *self, gint length, GError **error);
gboolean __mirage_parser_cue_add_session (MIRAGE_Parser *self, gint number, GError **error);

G_END_DECLS

#endif /* __IMAGE_CUE_PARSER_H__ */
