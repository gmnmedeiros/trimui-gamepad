.PHONY: build package verify install clean

build package:
	./scripts/build.sh

verify:
	./scripts/verify.sh

install: package verify
	./scripts/install.sh

clean:
	rm -rf .build dist
