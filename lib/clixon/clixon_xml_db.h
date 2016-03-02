/*
 *
  Copyright (C) 2009-2016 Olof Hagsand and Benny Holmgren

  This file is part of CLIXON.

  CLIXON is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  CLIXON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with CLIXON; see the file LICENSE.  If not, see
  <http://www.gnu.org/licenses/>.

 * XML support functions.
 */
#ifndef _CLIXON_XML_DB_H
#define _CLIXON_XML_DB_H

/*
 * Prototypes
 */
int yang2xmlkeyfmt(yang_stmt *ys, char **xkfmt);
int xmlkeyfmt2key(char *xkfmt, cvec *cvv, char **xk);
int xmlkeyfmt2xpath(char *xkfmt, cvec *cvv, char **xk);
int xmlkey2xml(char *xkey, yang_spec *yspec, char **xml);
int xmldb_get(char *dbname, char *xpath, 
	      yang_spec *yspec, cxobj **xtop);
int xmldb_get_vec(char *dbname, char *xpath, yang_spec *yspec,
		  cxobj **xtop, cxobj ***xvec, size_t *xlen);
int xmldb_put( char *dbname, cxobj *xt, 
	      yang_spec *yspec, enum operation_type op);
int xmldb_put_xkey(char *dbname, char *xkey, char *val, yang_spec *yspec, 
		   enum operation_type op);
int xmldb_dump(FILE *f, char *dbname, char *rxkey);
int xmldb_init(char *file);

#endif /* _CLIXON_XML_DB_H */