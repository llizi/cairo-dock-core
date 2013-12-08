/*
* This file is a part of the Cairo-Dock project
*
* Copyright : (C) see the 'copyright' file.
* E-mail    : see the 'copyright' file.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 3
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#ifndef __CAIRO_DOCK_KEYFILE_UTILITIES__
#define  __CAIRO_DOCK_KEYFILE_UTILITIES__

#include <glib.h>
#include "cairo-dock-struct.h"
#include "cairo-dock-gui-factory.h"  // CairoDockGUIWidgetType
G_BEGIN_DECLS


/**
*@file cairo-dock-keyfile-utilities.h This class provides useful functions to manipulate the conf files of Cairo-Dock, which are classic group/key pair files.
*/

/** Open a conf file to be read/written. Returns NULL if the file couldn't be found/opened/parsed.
*Free it with g_key_file_free after you're done.
*/
GKeyFile *cairo_dock_open_key_file (const gchar *cConfFilePath);

/** Write a key file on the disk.
*/
void cairo_dock_write_keys_to_file (GKeyFile *pKeyFile, const gchar *cConfFilePath);

/** Merge the values of a conf-file into another one. Keys are filtered by an identifier on the original conf-file.
*@param cConfFilePath an up-to-date conf-file with old values, that will be updated.
*@param cReplacementConfFilePath an old conf-file containing values we want to use
*@param iIdentifier a character to filter the keys, or 0.
*/
void cairo_dock_merge_conf_files (const gchar *cConfFilePath, gchar *cReplacementConfFilePath, gchar iIdentifier);

/** Update a conf-file, by merging values from a given key-file into a template conf-file.
*@param cConfFilePath path to the conf-file to update.
*@param pKeyFile a key-file with correct values, but old comments and possibly missing or old keys. It is not modified by the function.
*@param cDefaultConfFilePath a template conf-file.
*@param bUpdateKeys whether to remove old keys (hidden and persistent) or not.
*/
void cairo_dock_upgrade_conf_file_full (const gchar *cConfFilePath, GKeyFile *pKeyFile, const gchar *cDefaultConfFilePath, gboolean bUpdateKeys);
#define cairo_dock_upgrade_conf_file(cConfFilePath, pKeyFile, cDefaultConfFilePath) cairo_dock_upgrade_conf_file_full (cConfFilePath, pKeyFile, cDefaultConfFilePath, TRUE)

/** Get the version of a conf file. The version is written on the first line of the file, as a comment.
*/
void cairo_dock_get_conf_file_version (GKeyFile *pKeyFile, gchar **cConfFileVersion);

/** Say if a conf file's version mismatches a given version.
*/
gboolean cairo_dock_conf_file_needs_update (GKeyFile *pKeyFile, const gchar *cVersion);

/** Add or remove a value in a list of values to a given (group,key) pair of a conf file.
*/
void cairo_dock_add_remove_element_to_key (const gchar *cConfFilePath, const gchar *cGroupName, const gchar *cKeyName, gchar *cElementName, gboolean bAdd);

/** Add a key to a conf file, so that it can be parsed by the GUI manager.
*/
void cairo_dock_add_group_key_to_conf_file (GKeyFile *pKeyFile, const gchar *cGroupName, const gchar *ckeyName, const gchar *cInitialValue, CairoDockGUIWidgetType iWidgetType, const gchar *cAuthorizedValues, const gchar *cDescription, const gchar *cTooltip);

/** Remove a key from a conf file.
*/
void cairo_dock_remove_group_key_from_conf_file (GKeyFile *pKeyFile, const gchar *cGroupName, const gchar *ckeyName);

/* Change the name of a group in a conf file. Returns TRUE if changes have been made, FALSE otherwise.
*/
gboolean cairo_dock_rename_group_in_conf_file (GKeyFile *pKeyFile, const gchar *cGroupName, const gchar *cNewGroupName);

/* Used g_key_file_get_locale_string only if the key name exists and is not empty
 * Can be anoying to use it with an empty string because gettext mays return a non empty string (e.g. on OpenSUSE we get the .po header)
 */
gchar * cairo_dock_get_locale_string_from_conf_file (GKeyFile *pKeyFile, const gchar *cGroupName, const gchar *cKeyName, const gchar *cLocale);

void cairo_dock_update_keyfile_va_args (const gchar *cConfFilePath, GType iFirstDataType, va_list args);

/** Update a conf file with a list of values of the form : {type, name of the groupe, name of the key, value}. Must end with G_TYPE_INVALID.
*@param cConfFilePath path to the conf file.
*@param iFirstDataType type of the first value.
*/
void cairo_dock_update_keyfile (const gchar *cConfFilePath, GType iFirstDataType, ...);

G_END_DECLS
#endif
