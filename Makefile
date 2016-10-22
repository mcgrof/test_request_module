all: modules

allnoconfig:


.PHONY: modules
modules:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	#/lib/modules/$(shell uname -r)/build/scripts/sign-file \
#		sha1 \
#		/lib/modules/$(shell uname -r)/build/signing_key.priv \
#		/lib/modules/$(shell uname -r)/build/signing_key.x509 \
#		hello.ko

.PHONY: clean
clean:
	make -C /lib/modules/$(shell uname -r)/build clean M=$(PWD)

obj-m += test_req_mod.o
