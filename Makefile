BUILD   := build
DC_RUN  := docker compose run --rm dev

# cmake configure + build in one shot (skips configure if already done)
CMAKE_BUILD = [ -f $(BUILD)/CMakeCache.txt ] || cmake -S . -B $(BUILD); cmake --build $(BUILD) --parallel

.PHONY: all tests run_tests run run_server lint clean \
        docker-build docker-start docker-stop docker-logs

# ── Build ─────────────────────────────────────────────────────────────────────
all:
	$(DC_RUN) sh -c '$(CMAKE_BUILD)'

tests:
	$(DC_RUN) sh -c '$(CMAKE_BUILD) --target test_gale_shapley test_hopcroft_karp test_hungarian test_blossom test_db_types test_conn_pool test_db_manager'

# ── Run ───────────────────────────────────────────────────────────────────────
run:
	$(DC_RUN) sh -c '$(CMAKE_BUILD) && ./$(BUILD)/main'

run_server:
	docker compose up app

# ── Test ──────────────────────────────────────────────────────────────────────
run_tests:
	$(DC_RUN) sh -c '$(CMAKE_BUILD) --target test_gale_shapley test_hopcroft_karp test_hungarian test_blossom test_db_types test_conn_pool test_db_manager && \
	    for t in $(BUILD)/test_gale_shapley $(BUILD)/test_hopcroft_karp $(BUILD)/test_hungarian $(BUILD)/test_blossom $(BUILD)/test_db_types $(BUILD)/test_conn_pool $(BUILD)/test_db_manager; do \
	        echo "--- $$t ---"; ./$$t; echo; \
	    done'

# ── Lint ──────────────────────────────────────────────────────────────────────
lint:
	$(DC_RUN) sh -c '$(CMAKE_BUILD) && \
	    find src server -name "*.cpp" ! -path "*/tests/*" | \
	    xargs clang-tidy -p $(BUILD)'

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	$(DC_RUN) rm -rf $(BUILD)

# ── Docker Compose ────────────────────────────────────────────────────────────
docker-build:
	docker compose build

docker-start:
	docker compose up -d --build
	@echo "Server running at http://localhost:8081"

docker-stop:
	docker compose down

docker-logs:
	docker compose logs -f
