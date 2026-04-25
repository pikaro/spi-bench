PIO_ENV ?= master
COMPILE_DB := compiledb/$(PIO_ENV)/compile_commands.json
ENV_DATA := .pio/build/$(PIO_ENV)/idedata.json
WIRE_OUT := include/Generated/Wire
BINDINGS_OUT := include/Generated/Bindings

.PHONY: wire wire-clean bindings bindings-clean

$(COMPILE_DB):
	pio run -e $(PIO_ENV) -t compiledb

$(ENV_DATA):
	mkdir -p $(dir $@)
	pio run -e $(PIO_ENV) -t idedata | python3 bin/extract_idedata.py $@

wire: $(COMPILE_DB) $(ENV_DATA)
	python3 generate/wire.py --compdb $(COMPILE_DB) --envdata $(ENV_DATA) --out $(WIRE_OUT)

wire-clean:
	rm -rf $(WIRE_OUT)

bindings: $(COMPILE_DB) $(ENV_DATA)
	python3 generate/bindings.py --compdb $(COMPILE_DB) --envdata $(ENV_DATA) --out $(BINDINGS_OUT)

bindings-clean:
	rm -rf $(BINDINGS_OUT)

all: wire bindings
