all: a.out

a.out: lru_map.cpp
	g++-7 -o $@ -std=c++17 -O3 -g3 $<

profile: a.out
	rm -f ./p.prof
	CPUPROFILE=./p.prof LD_PRELOAD=/usr/lib/libprofiler.so.0 ./a.out
	google-pprof --focus='.*put.*' --focus='.*get.*' --pdf ./a.out ./p.prof > report.pdf

clean:
	rm -f a.out core.* core p.prof report.pdf
