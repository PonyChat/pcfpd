FPD = pcfpd
all: $(FPD)
clean:
	rm -f $(FPD)
$(FPD): $(FPD).c
	gcc -g -O2 -o $@ $<
