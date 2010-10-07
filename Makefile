#
#	@file Makefile		@brief core runtime configuration Makefile.
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

VERSION := "1.00"
GIT_REV := $(shell git describe --always 2>/dev/null)

CC	:= gcc
OPTIM	:= -march=native -O2 -fomit-frame-pointer
CFLAGS	= $(OPTIM) -W -Wall -Wextra -g -pipe -DCORE_RC_TEST -DDEBUG_CORE_RC \
	-DVERSION='$(VERSION)' $(if $(GIT_REV), -DGIT_REV='"$(GIT_REV)"')
#STATIC= --static
LIBS	= $(STATIC)

HDRS	:= core-rc.h
OBJS	:= core-rc.o
FILES	:= Makefile README.txt Changelog AGPL-3.0.txt core-rc.doxyfile

all:	rc_test

#----------------------------------------------------------------------------
#	Modules

core-array/core-array.mk:
	git submodule init core-array
	git submodule update core-array

include core-array/core-array.mk

#----------------------------------------------------------------------------

rc_test	: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

$(OBJS)	: Makefile 

core-rc_parser.c: core-rc_parser.peg core-rc_parser.c.in
	peg -o core-rc_parser.c core-rc_parser.peg \
		|| cp core-rc_parser.c.in core-rc_parser.c

core-rc.o:	core-rc_parser.c

#----------------------------------------------------------------------------
#	Developer tools

doc:	$(SRCS) $(HDRS) core-rc.doxyfile
	(cat core-rc.doxyfile; \
	echo 'PROJECT_NUMBER=${VERSION} $(if $(GIT_REV), (GIT-$(GIT_REV)))') \
	| doxygen -

indent:
	for i in $(OBJS:.o=.c) $(HDRS); do \
		indent $$i; unexpand -a $$i > $$i.up; mv $$i.up $$i; \
	done

clean:
	-rm *.o *~

clobber:	clean
	-rm -rf rc_test core-rc_parser.c www/html

dist:
	tar cjCf .. core-rc-`date +%F-%H`.tar.bz2 \
		$(addprefix core-rc/, $(FILES) $(HDRS) $(OBJS:.o=.c))

install:
	##strip --strip-unneeded -R .comment binary_file
	#install -s binary_file /usr/local/bin/

help:
	@echo "make all|doc|indent|clean|clobber|dist|install|help"
