
all: inicializer finalizer producer consumer releaser

inicializer:
	gcc -o inicializer SharedMemory.c -lrt -pthread   

finalizer: 
	gcc -o finalizer Finisher.c -lrt -pthread  

producer: 
	gcc -o producer Producer.c -lrt -pthread -lm  

consumer: 
	gcc -o consumer Consumer.c -lrt -pthread -lm
	
releaser:
	gcc -o releaser Releaser.c -lpthread  
