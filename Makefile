
default:	build

clean:
	rm -rf Makefile objs

build:
	$(MAKE) -f objs/Makefile

install:
	$(MAKE) -f objs/Makefile install

modules:
	$(MAKE) -f objs/Makefile modules

upgrade:
	/home/wgw/nginx-1.11.3/sbin -t

	kill -USR2 `cat /home/wgw/nginx-1.11.3/logs/nginx.pid`
	sleep 1
	test -f /home/wgw/nginx-1.11.3/logs/nginx.pid.oldbin

	kill -QUIT `cat /home/wgw/nginx-1.11.3/logs/nginx.pid.oldbin`
