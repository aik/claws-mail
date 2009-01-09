/* Claws Mail -- a GTK+ based, lightweight, and fast e-mail client
 * Copyright (C) 2007-2009 Holger Berndt <hb@claws-mail.org> 
 * and the Claws Mail team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. 
 */

#ifndef __ADDRDUPLICATES_H__
#define __ADDRDUPLICATES_H__

#include <gtk/gtk.h>

#include "addritem.h"
#include "addrindex.h"

void addrduplicates_find(GtkWindow *parent);
gboolean addrduplicates_delete_item_person(ItemPerson*, AddressDataSource*);

#endif /*__ADDRDUPLICATES_H__*/
