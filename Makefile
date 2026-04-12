PIO_ENV ?= master
COMPILE_DB := compiledb/$(PIO_ENV)/compile_commands.json
WIRE_OUT := include/Generated/Wire

.PHONY: wire wire-compdb wire-clean

wire: $(COMPILE_DB)
	python3 bin/generate_wire_fields.py --compdb $(COMPILE_DB) --out $(WIRE_OUT)

$(COMPILE_DB):
	pio run -e $(PIO_ENV) -t compiledb

wire-clean:
	rm -rf $(WIRE_OUT)
