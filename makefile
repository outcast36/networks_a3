make: 
	gcc -g prog3_server.c -o server
	gcc -g prog3_observer.c -o c1
	gcc -g prog3_participant.c -o c0

clean:
	rm -f server c0 c1
