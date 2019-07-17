pa-passthrough: pa-passthrough.cc
	g++ -Wall -O2 -g -o $@ $^ -lportaudio -lrnnoise-nu
