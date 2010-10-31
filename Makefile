CC = gcc
CFLAGS = -O2 -g -Wall -Wextra -Wno-unused-parameter -pedantic -pipe `pkg-config --cflags  glib-2.0`
LIBS = -Wl,-Bdynamic -lncurses -lutil -lglib-2.0 -lreadline
OBJDIR = .build
OBJECTS = main.o
OBJECTS :=  $(addprefix ${OBJDIR}/,${OBJECTS})

agora: $(OBJECTS)
	$(CC) $(CFLAGS) $(LIBS) $(OBJECTS) -o agora libvterm.a


${OBJDIR}/%.o : %.c
	@if [ ! -d $(OBJDIR) ]; then mkdir $(OBJDIR); fi #create directory if it doesn't exist
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) agora
