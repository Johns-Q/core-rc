#
#	@file core-rc.mk	@brief runtime configuration module makefile.
#
#	Copyright (c) 2010 by Lutz Sammer.  All Rights Reserved.
#
#	Contributor(s):
#
#	License: AGPLv3
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU Affero General Public License as
#	published by the Free Software Foundation, either version 3 of the
#	License.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU Affero General Public License for more details.
#
#	$Id$
#----------------------------------------------------------------------------

#	include core-rc/core-rc.mk in the main application Makefile

OBJS+=	core-rc/core-rc.o
HDRS+=	core-rc/core-rc.h
FILES+=	core-rc/core-rc_parser.peg core-rc/core-rc_parser.c.in \
	core-rc/core-rc.mk

core-rc/core-rc.o: core-rc/core-rc.c core-rc/core-rc_parser.c

core-rc/core-rc_parser.c: core-rc/core-rc_parser.peg core-rc/core-rc_parser.c.in
	peg -o core-rc/core-rc_parser.c core-rc/core-rc_parser.peg \
		|| cp core-rc/core-rc_parser.c.in core-rc/core-rc_parser.c

$(OBJS):core-rc/core-rc.mk

#----------------------------------------------------------------------------
#	Developer tools

.PHONY: core-rc-clean core-rc-clobber

core-rc-clean:
	-rm core-rc/core-rc.o

clean:	core-rc-clean

core-rc-clobber:
	-rm core-rc/core-rc_parser.c

clobber:	core-rc-clobber

