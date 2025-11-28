all:
	-make -C utils
	-make -C master
	-make -C storage
	-make -C worker
	-make -C query_control

clean:
	-make -C utils clean
	-make -C master clean
	-make -C storage clean
	-make -C worker clean
	-make -C query_control clean
