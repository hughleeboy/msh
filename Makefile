debug: msh

msh: msh.c
	gcc -g -o $@ $<

clean:
	rm msh
