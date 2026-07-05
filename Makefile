.PHONY: help build test test-asan test-tsan coverage test-integration \
        test-unit-image check clean

DOCKERFILE   := deploy/docker/Dockerfile
IMG_CI       := corvus-builder-ci
IMG_ASAN     := corvus-builder-asan
IMG_TSAN     := corvus-builder-tsan
IMG_COVERAGE := corvus-builder-coverage

help:
	@echo "Corvus build/test targets:"
	@echo "  make test              - fast unit tests (Release build, default preset)"
	@echo "  make test-asan         - unit tests under ASan + UBSan"
	@echo "  make test-tsan         - unit tests under TSan + UBSan (thread safety)"
	@echo "  make coverage          - unit tests with gcov instrumentation + report"
	@echo "  make test-integration  - docker compose up + pytest against live services"
	@echo "  make check             - test + test-asan + test-tsan + coverage (slow, pre-PR)"
	@echo "  make clean             - remove built docker images"

build:
	docker build -f $(DOCKERFILE) --target builder \
		--build-arg CMAKE_PRESET=ci \
		-t $(IMG_CI) .

test: build
	docker run --rm $(IMG_CI) \
		./build/tests/unit/corvus-unit-tests --gtest_color=yes

test-asan:
	docker build -f $(DOCKERFILE) --target builder \
		--build-arg CMAKE_PRESET=asan \
		-t $(IMG_ASAN) .
	docker run --rm -e ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		-e UBSAN_OPTIONS=halt_on_error=1 \
		$(IMG_ASAN) \
		./build-asan/tests/unit/corvus-unit-tests --gtest_color=yes

test-tsan:
	docker build -f $(DOCKERFILE) --target builder \
		--build-arg CMAKE_PRESET=tsan \
		-t $(IMG_TSAN) .
	docker run --rm -e TSAN_OPTIONS=halt_on_error=1 \
		-e UBSAN_OPTIONS=halt_on_error=1 \
		$(IMG_TSAN) \
		./build-tsan/tests/unit/corvus-unit-tests --gtest_color=yes

coverage:
	docker build -f $(DOCKERFILE) --target builder \
		--build-arg CMAKE_PRESET=coverage \
		-t $(IMG_COVERAGE) .
	docker run --rm $(IMG_COVERAGE) bash -c '\
		./build-coverage/tests/unit/corvus-unit-tests --gtest_color=yes && \
		gcovr --root . --filter "src/" --filter "include/" \
		--exclude ".*/build.*" --print-summary \
	'

test-integration:
	docker compose up -d
	@echo "Waiting for services to become healthy..."
	@sleep 10
	pytest tests/integration -v

check: test test-asan test-tsan coverage
	@echo "All checks passed."

clean:
	-docker rmi -f $(IMG_CI) $(IMG_ASAN) $(IMG_TSAN) $(IMG_COVERAGE)

fix-tsan-kernel:
	sudo sysctl -w vm.mmap_rnd_bits=28