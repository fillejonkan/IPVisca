PROG     = Axvisca

PKGS = gio-2.0 glib-2.0 cairo fixmath axptz axparameter axevent
CFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags $(PKGS)) -DGETTEXT_PACKAGE=\"libexif-12\" -DLOCALEDIR=\"\"
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(PKGS))
LDFLAGS  += -s -laxoverlay -laxevent -laxparameter -laxhttp

SRCS      = main.c ptz.c param.c vip.c
OBJS      = $(SRCS:.c=.o)

all: $(PROG) $(OBJS)

$(PROG): $(OBJS)
	$(CC) $^ $(CFLAGS) $(LIBS) $(LDFLAGS) -lm $(LDLIBS) -o $@
	$(STRIP) $@

clean:
	rm -f $(PROG) $(OBJS)

