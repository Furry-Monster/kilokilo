# kilokilo~
kilokilo: kilokilo.c
	$(CC) kilokilo.c -o kilokilo -Wall -Wextra -pedantic -std=c99

debug: kilokilo.c
	$(CC) kilokilo.c -g -o kilokilo -Wall -Wextra -pedantic -std=c99