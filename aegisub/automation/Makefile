include ../Makefile.inc

# share/
DATA_AUTOMATION += \
	autoload/clean-info.lua \
	autoload/cleantags-autoload.lua \
	autoload/kara-templater.lua \
	autoload/karaoke-auto-leadin.lua \
	autoload/macro-1-edgeblur.lua \
	autoload/macro-2-mkfullwitdh.lua \
	autoload/select-overlaps.lua \
	autoload/strip-tags.lua \
	include/cleantags.lua \
	include/clipboard.lua \
	include/karaskel-auto4.lua \
	include/karaskel.lua \
	include/re.lua \
	include/unicode.lua \
	include/utils-auto4.lua \
	include/utils.lua

# share/doc
DATA_GENERAL_DOC = \
	demos/raytracer-test1.ass \
	v4-docs/basic-function-interface.txt \
	v4-docs/configuration-dialogs.txt \
	v4-docs/file-streams.txt \
	v4-docs/misc.txt \
	v4-docs/overview.txt \
	v4-docs/progress-reporting.txt \
	v4-docs/subtitle-data.txt \
	v4-docs/template-scripting-ideas.ass

DATA_AUTOMATION_DOC += \
	demos/future-windy-blur.lua \
	demos/raytracer.lua

all: ;


install: install-share
	@$(BIN_MKDIR) -p $(addprefix $(DESTDIR)$(P_DATA)/automation/, $(dir $(DATA_AUTOMATION)))
	@for i in $(DATA_AUTOMATION); do \
		$(BIN_ECHO) "$(BIN_INSTALL) $$i $(DESTDIR)$(P_DATA)/automation/$$i"; \
		$(BIN_INSTALL) $$i $(DESTDIR)$(P_DATA)/automation/$$i; \
	done

DATA_SHARE_DOC	= $(DATA_GENERAL_DOC) $(DATA_AUTOMATION_DOC)

EXTRA_DIST = \
	$(DATA_AUTOMATION) \
	$(DATA_GENERAL_DOC) \
	$(DATA_AUTOMATION_DOC)

include ../Makefile.target
