PYTHON  = python3
PIP     = pip3
VENV    = .venv
BIN     = $(VENV)/bin

TARGET  = fit
PKG     = src
DIST    = dist

.PHONY: all
all: $(VENV)
	@echo "  Ready. Run: make run"

$(VENV): requirements.txt
	@echo "  VENV"
	$(PYTHON) -m venv $(VENV)
	$(BIN)/pip install --upgrade pip
	$(BIN)/pip install -r requirements.txt
	$(BIN)/pip install -e .
	@touch $(VENV)

.PHONY: run
run: $(VENV)
	@echo "  RUN  $(TARGET)"
	$(BIN)/python -m $(TARGET) $(ARGS)

.PHONY: test
test: $(VENV)
	@echo "  TEST"
	$(BIN)/pytest $(PKG) -v

.PHONY: lint
lint: $(VENV)
	@echo "  LINT"
	$(BIN)/ruff check $(PKG)
	$(BIN)/mypy $(PKG)

.PHONY: fmt
fmt: $(VENV)
	@echo "  FMT"
	$(BIN)/ruff format $(PKG)

.PHONY: build
build: $(VENV)
	@echo "  BUILD"
	$(BIN)/pip install build
	$(BIN)/python -m build

.PHONY: clean
clean:
	rm -rf $(VENV) $(DIST) __pycache__ .mypy_cache .ruff_cache .pytest_cache
	find . -type d -name "__pycache__" -exec rm -rf {} +
	find . -name "*.pyc" -delete
	@echo "  Cleaned."

PREFIX  = /usr/local
INSTALL_DIR = $(HOME)/.local/bin
.PHONY: install
install: bundle
	@echo "  INSTALL"
	cp dist/fit $(INSTALL_DIR)/fit
	chmod +x $(INSTALL_DIR)/fit
	@echo "  Installed to $(INSTALL_DIR)/fit"

.PHONY: uninstall
uninstall:
	rm -f $(INSTALL_DIR)/fit
	@echo "  Uninstalled $(INSTALL_DIR)/fit"

.PHONY: bundle
bundle: $(VENV)
	@echo "  BUNDLE"
	$(BIN)/pip install pyinstaller
	$(BIN)/pyinstaller --onefile --paths src \
		--hidden-import fit.refs \
		--hidden-import fit.objects \
		--hidden-import fit.repository \
		--hidden-import fit.index \
		--hidden-import fit.ignore \
		--hidden-import fit.config \
		--hidden-import fit.commands \
		--hidden-import fit.remote \
		src/fit/cli.py --name fit
	@echo "  Binary at dist/fit"

.PHONY: help
help:
	@echo "Targets:"
	@echo "  all       — create virtualenv and install dependencies (default)"
	@echo "  run       — run the app inside the venv  (pass ARGS=... to forward args)"
	@echo "  test      — run pytest"
	@echo "  lint      — ruff check + mypy"
	@echo "  fmt       — auto-format with ruff"
	@echo "  build     — build sdist + wheel into dist/"
	@echo "  clean     — remove venv, dist, caches"
	@echo "  install   — pip-install the built wheel system-wide"
	@echo "  uninstall — pip-uninstall the package"
