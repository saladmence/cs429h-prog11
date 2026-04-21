build: 
	gcc -g -I./include -o hw11 src/cache.c 
valgrind:
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./hw11
run: 
	hw11
test:
	gcc -o test tests/plc.c src/cache.c -I include -g
clean:
	rm -f hw11