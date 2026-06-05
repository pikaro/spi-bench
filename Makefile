PIO_ENV ?= master
COMPILE_DB := compiledb/$(PIO_ENV)/compile_commands.json
ENV_DATA := build/$(PIO_ENV)/idedata.json
WIRE_OUT := include/Generated/Wire
PY_WIRE_OUT := tools/wire_models_py/totem_wire/generated.py
BINDINGS_OUT := include/Generated/Bindings

.PHONY: wire wire-clean bindings bindings-clean led-render-registry

$(COMPILE_DB):
	pio run -e $(PIO_ENV) -t compiledb

$(ENV_DATA):
	mkdir -p $(dir $@)
	pio run -e $(PIO_ENV) -t idedata | python3 bin/extract_idedata.py $@

wire: $(COMPILE_DB) $(ENV_DATA)
	python3 generate/wire.py --compdb $(COMPILE_DB) --envdata $(ENV_DATA) --out $(WIRE_OUT) --py-out $(PY_WIRE_OUT)

wire-clean:
	rm -rf $(WIRE_OUT)
	rm -f $(PY_WIRE_OUT)

bindings: $(COMPILE_DB) $(ENV_DATA)
	python3 generate/bindings.py --compdb $(COMPILE_DB) --envdata $(ENV_DATA) --out $(BINDINGS_OUT)

bindings-clean:
	rm -rf $(BINDINGS_OUT)

led-render-registry:
	python3 tools/led-render/generate_registry.py --root . --out tools/led-render/generated/AnimationRegistry.hpp

all: wire bindings
