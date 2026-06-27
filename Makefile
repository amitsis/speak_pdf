NAME=speak_pdf
VERSION=1.1
PREFIX?=/usr
BINDIR?=$(PREFIX)/bin
MANDIR?=$(PREFIX)/share/man
MAN1DIR?=$(MANDIR)/man1
PYTHON?=python3
INSTALL?=install

all: man/$(NAME).1.gz

man/$(NAME).1.gz: man/$(NAME).1
	gzip -9 -c man/$(NAME).1 > man/$(NAME).1.gz

check:
	$(PYTHON) -m py_compile src/$(NAME).py
	grep -q '^\.TH SPEAK_PDF 1' man/$(NAME).1
	gzip -t man/$(NAME).1.gz
	@echo "Basic checks passed."

install: all
	$(INSTALL) -Dm755 src/$(NAME).py $(DESTDIR)$(BINDIR)/$(NAME)
	$(INSTALL) -Dm644 man/$(NAME).1.gz $(DESTDIR)$(MAN1DIR)/$(NAME).1.gz

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(NAME)
	rm -f $(DESTDIR)$(MAN1DIR)/$(NAME).1.gz

dist: clean
	cd .. && tar --exclude-vcs -czf $(NAME).tar.gz $(NAME)

rpm: dist
	rpmdev-setuptree
	cp ../$(NAME).tar.gz $(HOME)/rpmbuild/SOURCES/
	cp packaging/rpm/$(NAME).spec $(HOME)/rpmbuild/SPECS/
	rpmbuild -ba $(HOME)/rpmbuild/SPECS/$(NAME).spec

clean:
	rm -f man/$(NAME).1.gz

.PHONY: all check install uninstall dist rpm clean
