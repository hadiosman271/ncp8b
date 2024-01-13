ncp8b: player.c media.c media.h log.c log.h
	cc -g -o ncp8b player.c media.c log.c -lavformat -lavcodec -lavutil -lswscale -lncurses
