CC ?= cc
PYTHON ?= python3
CFLAGS ?= -O2 -g -std=c99 -Wall -Wextra -Werror -pedantic

.PHONY: all test clean
all:
	$(PYTHON) scripts/build.py --cc "$(CC)" --cflags="$(CFLAGS)"

test: all
	PYTHONPATH=python $(PYTHON) -m unittest discover -s tests -v

clean:
	$(PYTHON) -c "import shutil; shutil.rmtree('build', ignore_errors=True)"
