program: main.o functions.o mobile_node.o
	 gcc -Wall -pthread main.o functions.o -o offload_simulator
	 gcc -Wall mobile_node.o -o mobile_node
	 rm main.o functions.o mobile_node.o

main.o: main.c declarations.h
	gcc -c main.c

functions.o: functions.c declarations.h
	gcc -c functions.c

mobile_node.o: mobile_node.c
	gcc -c mobile_node.c