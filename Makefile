pa-passthrough: pa-passthrough.cc terminal.c
	g++ -Wall -O2 -g -o $@ $^ -lportaudio -lrnnoise-nu -Wl,-rpath,/usr/local/lib
