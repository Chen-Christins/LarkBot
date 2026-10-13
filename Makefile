define time_wrapper
	echo "开始时间: $$(date '+%Y-%m-%d %H:%M:%S.%3N')" && \
	start_time=$$(date +%s%3N) && \
	$(1) && \
	end_time=$$(date +%s%3N) && \
	duration=$$((end_time - start_time)) && \
	echo "结束时间: $$(date '+%Y-%m-%d %H:%M:%S.%3N')" && \
	echo "总计耗时: $$(echo "scale=3; $$duration/1000" | bc) 秒"
endef

# ---- 构建 ----
#   make / make build   → 全量构建（默认）
#   make clean          → 清理并删除 build/ 目录
#   make deps           → 下载 chen-sdk + 三方依赖（deps.py sync）
#   make apply          → 用 SDK 的 apply.sh 同步 bin/tools/protocol/resources
#   make proto          → 由 protocol/xml 生成 protocol/include 头文件
#   make collect [ENV=prod] → 打包部署产物
#   make dist [ENV=prod]    → deps + apply + proto + build + collect 一条龙

.DEFAULT_GOAL := build

_NPROC := $(shell nproc)
_JOBS  ?= $(shell expr $(_NPROC) - 1)
_JOB_CMD = _J="$$(echo "$(MAKEFLAGS)" | sed -n 's/.*-j[[:space:]]*\([0-9]\{1,\}\).*/\1/p')"

.PHONY: build
build:
	@$(_JOB_CMD) && \
	$(call time_wrapper, \
		if [ -d "build" ]; then \
			cd build && cmake -G Ninja .. && ninja -j"$${_J:-$(_JOBS)}"; \
		else \
			mkdir -p build && cd build && cmake -G Ninja .. && ninja -j"$${_J:-$(_JOBS)}"; \
		fi)

%:
	@$(_JOB_CMD) && \
	$(call time_wrapper, \
		if [ -d "build" ]; then \
			cd build && cmake -G Ninja .. && ninja -j"$${_J:-$(_JOBS)}" $@; \
		else \
			mkdir -p build && cd build && cmake -G Ninja .. && ninja -j"$${_J:-$(_JOBS)}" $@; \
		fi)

clean:
	@$(_JOB_CMD) && \
	$(call time_wrapper, \
		if [ -d "build" ]; then \
			cd build && cmake -G Ninja .. && ninja clean && cd ..; \
			rm -rf build; \
		fi)

.PHONY: deps apply proto collect dist
deps:
	@python3 deps.py sync

apply:
	@set -e; found=0; \
	for d in chen-sdk-*/; do \
		[ -f "$${d}apply.sh" ] || continue; \
		sh "$${d}apply.sh"; found=1; \
	done; \
	[ "$$found" = "1" ] || { echo "错误: 未找到 chen-sdk-*/apply.sh，请先执行 make deps" >&2; exit 1; }

proto:
	@set -e; \
	CHEN_SDK_LIB="$$(for d in chen-sdk-*/lib; do [ -d "$$d" ] && printf '%s' "$$d" && break; done)"; \
	LD_LIBRARY_PATH="$$CHEN_SDK_LIB:$$LD_LIBRARY_PATH" $(MAKE) -C protocol convert_only

collect:
	@./collect.sh $(if $(ENV),$(ENV),prod)

dist:
	@$(MAKE) deps
	@$(MAKE) apply
	@$(MAKE) proto
	@$(MAKE) build
	@$(MAKE) collect ENV=$(if $(ENV),$(ENV),prod)
