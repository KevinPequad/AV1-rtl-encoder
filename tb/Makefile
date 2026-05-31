# ---------------------------------------------------------------------------
# Makefile -- Build Verilator simulation for av1_encoder_top
#
# Usage:
#   make              Build the simulator binary
#   make run          Build and run with defaults (1 frame)
#   make wave         Build and run with VCD tracing
#   make clean        Remove build artifacts
#
# Override on command line:
#   make run FRAMES=10 TIMEOUT=500000000
#   make run YUV_INPUT=/path/to/file.yuv OBU_OUTPUT=/path/to/out.obu
#   make WIDTH=1280 HEIGHT=720 run FRAMES=240
# ---------------------------------------------------------------------------

VERILATOR       ?= verilator
CXX             ?= g++
LINK            ?= $(CXX)
AR              ?= ar
TOP_MODULE      := av1_encoder_top
TB_CPP          := tb_av1_encoder.cpp
ENTROPY_TOP     := av1_entropy
ENTROPY_TB_CPP  := test_entropy.cpp
BITSTREAM_TOP   := av1_bitstream
BITSTREAM_TB_CPP:= test_rtl_bitstream.cpp
INV_XFORM_TOP   := av1_inverse_transform
INV_XFORM_TB_CPP:= test_inv_transform.cpp
INTER_TOP       := av1_inter_pred
INTER_TB_CPP    := test_inter_pred.cpp
ME_TOP          := av1_me
ME_TB_CPP       := test_me.cpp
CHROMA_INTER_TOP := av1_chroma_inter_pred
CHROMA_INTER_TB_CPP := test_chroma_inter_pred.cpp
CHROMA_RES_TOP := av1_chroma_residual
CHROMA_RES_TB_CPP := test_chroma_residual.cpp
CHROMA_COEFF_TABLE_TB_CPP := test_chroma_coeff_tables.cpp
CHROMA_COEFF_TABLE_BIN ?= test_chroma_coeff_tables
RTL_DIR         := ../rtl
OBJ_DIR         := obj_dir
TRACE_OBJ_DIR   := obj_dir_trace
SIM_BIN         := V$(TOP_MODULE)
TRACE_SIM_BIN   := V$(TOP_MODULE)_trace
ENTROPY_OBJ_DIR := obj_dir_entropy
ENTROPY_SIM_BIN := V$(ENTROPY_TOP)
BITSTREAM_OBJ_DIR := obj_dir_bitstream
BITSTREAM_SIM_BIN := V$(BITSTREAM_TOP)
INV_XFORM_OBJ_DIR := obj_dir_inv_xform
INV_XFORM_SIM_BIN := V$(INV_XFORM_TOP)
INTER_OBJ_DIR   := obj_dir_inter_pred
INTER_SIM_BIN   := V$(INTER_TOP)
ME_OBJ_DIR      := obj_dir_me
ME_SIM_BIN      := V$(ME_TOP)
CHROMA_INTER_OBJ_DIR := obj_dir_chroma_inter_pred
CHROMA_INTER_SIM_BIN := V$(CHROMA_INTER_TOP)
CHROMA_RES_OBJ_DIR := obj_dir_chroma_residual
CHROMA_RES_SIM_BIN := V$(CHROMA_RES_TOP)

# Resolution (override on command line for different sizes)
WIDTH           ?= 1280
HEIGHT          ?= 720
THREADS         ?= $(shell nproc 2>/dev/null || echo 24)
BUILD_JOBS      ?= $(THREADS)
# Verilator's top-level --threads=$(THREADS) model is dramatically slower on
# these reduced AV1 syntax/public-decoder gates than a single-threaded model.
# Keep compile parallelism high, but run the top model single-threaded.
# The generated top-level obj_dir build also intermittently hits missing .d/.gch
# races at -j16 on Chud PC 2, so serialize just that inner make step.
SIM_THREADS     ?= 1
SIM_BUILD_JOBS  ?= 1
TOP_CONFIG_STAMP := $(OBJ_DIR)/.config-W$(WIDTH)-H$(HEIGHT)-T$(SIM_THREADS)


# Collect all Verilog sources from the RTL directory
RTL_SRCS        := $(wildcard $(RTL_DIR)/*.v)

# Verilator flags
VERILATOR_COMMON_FLAGS := --cc --exe \
    -Wall -Wno-fatal \
    --build-jobs $(BUILD_JOBS) \
    --threads $(SIM_THREADS) \
    --top-module $(TOP_MODULE) \
    -I$(RTL_DIR) \
    -GFRAME_WIDTH=$(WIDTH) -GFRAME_HEIGHT=$(HEIGHT)

VERILATOR_FLAGS := $(VERILATOR_COMMON_FLAGS) \
    -Mdir $(OBJ_DIR) \
    -CFLAGS "-O2 -std=c++17 -DFRAME_W=$(WIDTH) -DFRAME_H=$(HEIGHT) -DVERILATOR_THREADS=$(SIM_THREADS)"

VERILATOR_TRACE_FLAGS := $(VERILATOR_COMMON_FLAGS) \
    --trace \
    -Mdir $(TRACE_OBJ_DIR) \
    -CFLAGS "-O2 -std=c++17 -DFRAME_W=$(WIDTH) -DFRAME_H=$(HEIGHT) -DVERILATOR_THREADS=$(SIM_THREADS)"

# Runtime defaults (override on command line)
FRAMES          ?= 1
TIMEOUT         ?= 500000000
QINDEX          ?= 128
YUV_INPUT       ?= /workspace/data/raw_frames.yuv
OBU_OUTPUT      ?= /workspace/output/encoded.obu
EXTRA_PLUSARGS   ?=

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------

.PHONY: all run wave entropy-check bitstream-check header-qindex-sweep-check deterministic-packaging-check superblock-geometry-syntax-check qindex-edge-syntax-check inv-xform-check inter-pred-check me-check chroma-inter-pred-check chroma-residual-check chroma-coeff-table-check top-chroma-integration-check nonzero-chroma-syntax-check nonzero-chroma16-syntax-check natural32-chroma-syntax-check natural64-chroma-syntax-check natural32-ip-syntax-check natural32-ip-newmv-syntax-check natural32-ip-fullcoeff-newmv-syntax-check natural64-ip-fullcoeff-newmv-syntax-check natural64-ip-fullcoeff-newmv-boundary52-probe-check natural64-ip-fullcoeff-newmv-3frame-probe-check natural64-ip-fullcoeff-newmv-4frame-syntax-check natural64-ip-fullcoeff-newmv-5frame-syntax-check natural64-ip-fullcoeff-newmv-6frame-syntax-check natural64-ip-fullcoeff-newmv-7frame-syntax-check natural64-ip-fullcoeff-newmv-8frame-syntax-check natural64-ip-fullcoeff-newmv-9frame-syntax-check natural64-ip-fullcoeff-newmv-10frame-syntax-check natural64-ip-fullcoeff-newmv-11frame-syntax-check natural64-ip-fullcoeff-newmv-12frame-syntax-check natural64-ip-fullcoeff-newmv-13frame-syntax-check natural64-ip-fullcoeff-newmv-14frame-syntax-check natural64-ip-fullcoeff-newmv-15frame-syntax-check natural32-ip-fractional-syntax-check natural32-ip-chroma-delta-syntax-check natural64-ip-fractional-syntax-check natural64-ip-mode-context-syntax-check gop-lifecycle-syntax-check rtl-byte-owner-check negative-missing-rtl-raw-check negative-testbench-repair-check negative-decoder-source-check negative-invalid-shared-oracle-check negative-obu-length-repair-check standalone-matrix-check top-public-matrix-check av1-current-matrix-check av1-regression-matrix partition-tree-current8x8-check qindex-sweep-lossy-check skip-txb-cbp-matrix-check quant-header-disabled-check p5-highdc-q1-public-check clean help

all: $(SIM_BIN)

$(TOP_CONFIG_STAMP):
	@echo "Configuring $(SIM_BIN) for WIDTH=$(WIDTH) HEIGHT=$(HEIGHT) SIM_THREADS=$(SIM_THREADS)"
	@rm -rf $(OBJ_DIR) $(SIM_BIN)
	@mkdir -p $(OBJ_DIR)
	@touch $@

$(SIM_BIN): $(TB_CPP) $(RTL_SRCS) $(TOP_CONFIG_STAMP)
	$(VERILATOR) $(VERILATOR_FLAGS) \
		$(RTL_SRCS) \
		$(TB_CPP) \
		-o $(SIM_BIN)
	$(MAKE) -C $(OBJ_DIR) -f V$(TOP_MODULE).mk -j $(SIM_BUILD_JOBS) AR="$(AR)" CXX="$(CXX)" LINK="$(LINK)"
	@cp -f $(OBJ_DIR)/$(SIM_BIN) ./$(SIM_BIN) 2>/dev/null || true
	@cp -f $(OBJ_DIR)/$(SIM_BIN) ./Vav1_encoder_top 2>/dev/null || true
	@echo ""
	@echo "Build complete: ./$(SIM_BIN)"

$(TRACE_SIM_BIN): $(TB_CPP) $(RTL_SRCS)
	$(VERILATOR) $(VERILATOR_TRACE_FLAGS) \
		$(RTL_SRCS) \
		$(TB_CPP) \
		-o $(TRACE_SIM_BIN)
	$(MAKE) -C $(TRACE_OBJ_DIR) -f V$(TOP_MODULE).mk -j $(SIM_BUILD_JOBS) AR="$(AR)" CXX="$(CXX)" LINK="$(LINK)"
	@cp -f $(TRACE_OBJ_DIR)/$(TRACE_SIM_BIN) ./$(TRACE_SIM_BIN) 2>/dev/null || true
	@echo ""
	@echo "Build complete: ./$(TRACE_SIM_BIN)"

$(ENTROPY_SIM_BIN): $(ENTROPY_TB_CPP) $(RTL_DIR)/av1_entropy.v av1_bitstream_writer.h
	$(VERILATOR) --cc --exe \
		-Wall -Wno-fatal \
		--build-jobs $(BUILD_JOBS) \
		--threads $(THREADS) \
		--top-module $(ENTROPY_TOP) \
		-I$(RTL_DIR) \
		-Mdir $(ENTROPY_OBJ_DIR) \
		-CFLAGS "-O2 -std=c++17" \
		$(RTL_DIR)/av1_entropy.v \
		$(ENTROPY_TB_CPP) \
		-o $(ENTROPY_SIM_BIN)
	$(MAKE) -C $(ENTROPY_OBJ_DIR) -f V$(ENTROPY_TOP).mk -j $(BUILD_JOBS) AR="$(AR)" CXX="$(CXX)" LINK="$(LINK)"
	@cp -f $(ENTROPY_OBJ_DIR)/$(ENTROPY_SIM_BIN) ./$(ENTROPY_SIM_BIN) 2>/dev/null || true
	@echo ""
	@echo "Build complete: ./$(ENTROPY_SIM_BIN)"

$(BITSTREAM_SIM_BIN): $(BITSTREAM_TB_CPP) $(RTL_DIR)/av1_bitstream.v
	$(VERILATOR) --cc --exe \
		-Wall -Wno-fatal \
		--build-jobs $(BUILD_JOBS) \
		--threads $(THREADS) \
		--top-module $(BITSTREAM_TOP) \
		-I$(RTL_DIR) \
		-GFRAME_WIDTH=$(WIDTH) -GFRAME_HEIGHT=$(HEIGHT) \
		-Mdir $(BITSTREAM_OBJ_DIR) \
		-CFLAGS "-O2 -std=c++17 -DFRAME_W=$(WIDTH) -DFRAME_H=$(HEIGHT) -DVERILATOR_THREADS=$(SIM_THREADS)" \
		$(RTL_DIR)/av1_bitstream.v \
		$(BITSTREAM_TB_CPP) \
		-o $(BITSTREAM_SIM_BIN)
	$(MAKE) -C $(BITSTREAM_OBJ_DIR) -f V$(BITSTREAM_TOP).mk -j $(BUILD_JOBS) AR="$(AR)" CXX="$(CXX)" LINK="$(LINK)"
	@cp -f $(BITSTREAM_OBJ_DIR)/$(BITSTREAM_SIM_BIN) ./$(BITSTREAM_SIM_BIN) 2>/dev/null || true
	@echo ""
	@echo "Build complete: ./$(BITSTREAM_SIM_BIN)"



$(ME_SIM_BIN): $(ME_TB_CPP) $(RTL_DIR)/av1_me.v $(RTL_DIR)/av1_inter_pred.v
	$(VERILATOR) --cc --exe 		-Wall -Wno-fatal 		--build-jobs $(BUILD_JOBS) 		--threads $(THREADS) 		--top-module $(ME_TOP) 		-I$(RTL_DIR) 		-GFRAME_WIDTH=$(WIDTH) -GFRAME_HEIGHT=$(HEIGHT) -GSEARCH_RANGE=2 		-Mdir $(ME_OBJ_DIR) 		-CFLAGS "-O2 -std=c++17 -DFRAME_W=$(WIDTH) -DFRAME_H=$(HEIGHT) -DVERILATOR_THREADS=$(SIM_THREADS)" 		$(RTL_DIR)/av1_inter_pred.v 		$(RTL_DIR)/av1_me.v 		$(ME_TB_CPP) 		-o $(ME_SIM_BIN)
	$(MAKE) -C $(ME_OBJ_DIR) -f V$(ME_TOP).mk -j $(BUILD_JOBS) AR="$(AR)" CXX="$(CXX)" LINK="$(LINK)"
	@cp -f $(ME_OBJ_DIR)/$(ME_SIM_BIN) ./$(ME_SIM_BIN) 2>/dev/null || true
	@echo ""
	@echo "Build complete: ./$(ME_SIM_BIN)"

$(INTER_SIM_BIN): $(INTER_TB_CPP) $(RTL_DIR)/av1_inter_pred.v
	$(VERILATOR) --cc --exe 		-Wall -Wno-fatal 		--build-jobs $(BUILD_JOBS) 		--threads $(THREADS) 		--top-module $(INTER_TOP) 		-I$(RTL_DIR) 		-GFRAME_WIDTH=$(WIDTH) -GFRAME_HEIGHT=$(HEIGHT) 		-Mdir $(INTER_OBJ_DIR) 		-CFLAGS "-O2 -std=c++17 -DFRAME_W=$(WIDTH) -DFRAME_H=$(HEIGHT) -DVERILATOR_THREADS=$(SIM_THREADS)" 		$(RTL_DIR)/av1_inter_pred.v 		$(INTER_TB_CPP) 		-o $(INTER_SIM_BIN)
	$(MAKE) -C $(INTER_OBJ_DIR) -f V$(INTER_TOP).mk -j $(BUILD_JOBS) AR="$(AR)" CXX="$(CXX)" LINK="$(LINK)"
	@cp -f $(INTER_OBJ_DIR)/$(INTER_SIM_BIN) ./$(INTER_SIM_BIN) 2>/dev/null || true
	@echo ""
	@echo "Build complete: ./$(INTER_SIM_BIN)"

$(INV_XFORM_SIM_BIN): $(INV_XFORM_TB_CPP) $(RTL_DIR)/av1_inverse_transform.v
	$(VERILATOR) --cc --exe \
		-Wall -Wno-fatal \
		--build-jobs $(BUILD_JOBS) \
		--threads $(THREADS) \
		--top-module $(INV_XFORM_TOP) \
		-I$(RTL_DIR) \
		-Mdir $(INV_XFORM_OBJ_DIR) \
		-CFLAGS "-O2 -std=c++17" \
		$(RTL_DIR)/av1_inverse_transform.v \
		$(INV_XFORM_TB_CPP) \
		-o $(INV_XFORM_SIM_BIN)
	$(MAKE) -C $(INV_XFORM_OBJ_DIR) -f V$(INV_XFORM_TOP).mk -j $(BUILD_JOBS) AR="$(AR)" CXX="$(CXX)" LINK="$(LINK)"
	@cp -f $(INV_XFORM_OBJ_DIR)/$(INV_XFORM_SIM_BIN) ./$(INV_XFORM_SIM_BIN) 2>/dev/null || true
	@echo ""
	@echo "Build complete: ./$(INV_XFORM_SIM_BIN)"

run: $(SIM_BIN)
	@mkdir -p $(dir $(OBU_OUTPUT))
	./$(SIM_BIN) \
		+frames=$(FRAMES) \
		+timeout=$(TIMEOUT) \
		+qindex=$(QINDEX) \
		+input=$(YUV_INPUT) \
		+output=$(OBU_OUTPUT) \
		$(EXTRA_PLUSARGS)

wave: $(TRACE_SIM_BIN)
	@mkdir -p $(dir $(OBU_OUTPUT))
	./$(TRACE_SIM_BIN) \
		+frames=$(FRAMES) \
		+timeout=$(TIMEOUT) \
		+qindex=$(QINDEX) \
		+input=$(YUV_INPUT) \
		+output=$(OBU_OUTPUT) \
		+trace \
		$(EXTRA_PLUSARGS)

entropy-check: $(ENTROPY_SIM_BIN)
	./$(ENTROPY_SIM_BIN)

bitstream-check: $(BITSTREAM_SIM_BIN)
	./$(BITSTREAM_SIM_BIN)

header-qindex-sweep-check:
	@set -e; \
	for geom in 8x8 16x16 32x32 64x64 128x64 64x128 128x128; do \
		w=$${geom%x*}; h=$${geom#*x}; \
		echo "[SWEEP] header/qindex $$geom"; \
		rm -rf $(BITSTREAM_OBJ_DIR) $(BITSTREAM_SIM_BIN); \
		$(MAKE) WIDTH=$$w HEIGHT=$$h THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) bitstream-check; \
	done

deterministic-packaging-check:
	$(MAKE) clean
	$(MAKE) WIDTH=16 HEIGHT=16 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	WIDTH=16 HEIGHT=16 python3 test_deterministic_packaging.py

superblock-geometry-syntax-check:
	@set -e; \
	for geom in 16x16 32x32 64x64 128x64 64x128 128x128; do \
		w=$${geom%x*}; h=$${geom#*x}; \
		echo "[SWEEP] superblock/geometry $$geom"; \
		$(MAKE) clean; \
		$(MAKE) WIDTH=$$w HEIGHT=$$h THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all; \
		WIDTH=$$w HEIGHT=$$h python3 test_superblock_geometry_syntax.py; \
	done

qindex-edge-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=16 HEIGHT=16 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	WIDTH=16 HEIGHT=16 python3 test_qindex_edge_syntax.py

inv-xform-check: $(INV_XFORM_SIM_BIN)
	./$(INV_XFORM_SIM_BIN)

inter-pred-check: $(INTER_SIM_BIN)
	./$(INTER_SIM_BIN)


$(CHROMA_INTER_SIM_BIN): $(CHROMA_INTER_TB_CPP) $(RTL_DIR)/av1_chroma_inter_pred.v
	$(VERILATOR) --cc --exe 		-Wall -Wno-fatal 		--build-jobs $(BUILD_JOBS) 		--threads $(THREADS) 		--top-module $(CHROMA_INTER_TOP) 		-I$(RTL_DIR) 		-GCHROMA_WIDTH=$(shell expr $(WIDTH) / 2) -GCHROMA_HEIGHT=$(shell expr $(HEIGHT) / 2) 		-Mdir $(CHROMA_INTER_OBJ_DIR) 		-CFLAGS "-O2 -std=c++17 -DFRAME_W=$(WIDTH) -DFRAME_H=$(HEIGHT) -DVERILATOR_THREADS=$(SIM_THREADS)" 		$(RTL_DIR)/av1_chroma_inter_pred.v 		$(CHROMA_INTER_TB_CPP) 		-o $(CHROMA_INTER_SIM_BIN)
	$(MAKE) -C $(CHROMA_INTER_OBJ_DIR) -f V$(CHROMA_INTER_TOP).mk -j $(BUILD_JOBS) AR="$(AR)" CXX="$(CXX)" LINK="$(LINK)"
	@cp -f $(CHROMA_INTER_OBJ_DIR)/$(CHROMA_INTER_SIM_BIN) ./$(CHROMA_INTER_SIM_BIN) 2>/dev/null || true
	@echo ""
	@echo "Build complete: ./$(CHROMA_INTER_SIM_BIN)"

chroma-inter-pred-check: $(CHROMA_INTER_SIM_BIN)
	./$(CHROMA_INTER_SIM_BIN)

me-check: $(ME_SIM_BIN)
	./$(ME_SIM_BIN)

$(CHROMA_RES_SIM_BIN): $(CHROMA_RES_TB_CPP) $(RTL_DIR)/av1_chroma_residual.v $(RTL_DIR)/av1_transform.v $(RTL_DIR)/av1_quantize.v $(RTL_DIR)/av1_inverse_quant.v $(RTL_DIR)/av1_inverse_transform.v
	$(VERILATOR) --cc --exe \
		-Wall -Wno-fatal \
		--build-jobs $(BUILD_JOBS) \
		--threads $(THREADS) \
		--top-module $(CHROMA_RES_TOP) \
		-I$(RTL_DIR) \
		-Mdir $(CHROMA_RES_OBJ_DIR) \
		-CFLAGS "-O2 -std=c++17" \
		$(RTL_DIR)/av1_transform.v \
		$(RTL_DIR)/av1_quantize.v \
		$(RTL_DIR)/av1_inverse_quant.v \
		$(RTL_DIR)/av1_inverse_transform.v \
		$(RTL_DIR)/av1_chroma_residual.v \
		$(CHROMA_RES_TB_CPP) \
		-o $(CHROMA_RES_SIM_BIN)
	$(MAKE) -C $(CHROMA_RES_OBJ_DIR) -f V$(CHROMA_RES_TOP).mk -j $(BUILD_JOBS) AR="$(AR)" CXX="$(CXX)" LINK="$(LINK)"
	@cp -f $(CHROMA_RES_OBJ_DIR)/$(CHROMA_RES_SIM_BIN) ./$(CHROMA_RES_SIM_BIN) 2>/dev/null || true
	@echo ""
	@echo "Build complete: ./$(CHROMA_RES_SIM_BIN)"

chroma-residual-check: $(CHROMA_RES_SIM_BIN)
	./$(CHROMA_RES_SIM_BIN)

chroma-coeff-table-check: $(CHROMA_COEFF_TABLE_TB_CPP) av1_tx4x4_chroma_tables.h
	$(CXX) -O2 -std=c++17 $(CHROMA_COEFF_TABLE_TB_CPP) -o $(CHROMA_COEFF_TABLE_BIN)
	./$(CHROMA_COEFF_TABLE_BIN)

top-chroma-integration-check:
	python3 test_top_chroma_integration.py

nonzero-chroma-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=8 HEIGHT=8 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_nonzero_chroma_syntax.py

nonzero-chroma16-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=16 HEIGHT=16 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_nonzero_chroma16_syntax.py

natural32-chroma-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=32 HEIGHT=32 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural32_chroma_syntax.py

natural64-chroma-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_chroma_syntax.py

natural32-ip-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=32 HEIGHT=32 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural32_ip_syntax.py

natural32-ip-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=32 HEIGHT=32 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural32_ip_newmv_syntax.py

natural32-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=32 HEIGHT=32 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural32_ip_fullcoeff_newmv_syntax.py

natural64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_syntax.py

natural64-ip-fullcoeff-newmv-boundary52-probe-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_boundary52_probe.py

natural64-ip-fullcoeff-newmv-3frame-probe-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_3frame_probe.py

natural64-ip-fullcoeff-newmv-4frame-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_4frame_syntax.py

natural64-ip-fullcoeff-newmv-5frame-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_5frame_syntax.py

natural64-ip-fullcoeff-newmv-6frame-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_6frame_syntax.py

natural64-ip-fullcoeff-newmv-7frame-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_7frame_syntax.py

natural64-ip-fullcoeff-newmv-8frame-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_8frame_syntax.py

natural64-ip-fullcoeff-newmv-9frame-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_9frame_syntax.py

natural64-ip-fullcoeff-newmv-10frame-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_10frame_syntax.py

natural64-ip-fullcoeff-newmv-11frame-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_11frame_syntax.py

natural64-ip-fullcoeff-newmv-12frame-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_12frame_syntax.py

natural64-ip-fullcoeff-newmv-13frame-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_13frame_syntax.py

natural64-ip-fullcoeff-newmv-14frame-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_14frame_syntax.py

natural64-ip-fullcoeff-newmv-15frame-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_15frame_syntax.py

natural32-ip-fractional-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=32 HEIGHT=32 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural32_ip_fractional_syntax.py

gop-lifecycle-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_gop_lifecycle_syntax.py

partition-tree-current8x8-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_p4_partition_tree.py

qindex-sweep-lossy-check:
	$(MAKE) clean
	$(MAKE) WIDTH=8 HEIGHT=8 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_p4_qindex_sweep.py

skip-txb-cbp-matrix-check:
	$(MAKE) clean
	$(MAKE) WIDTH=8 HEIGHT=8 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_p4_skip_txb_cbp_matrix.py

quant-header-disabled-check:
	python3 test_p4_quant_header_disabled.py

p5-highdc-q1-public-check:
	$(MAKE) clean
	$(MAKE) WIDTH=16 HEIGHT=16 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	AV1_TOP_SIM=./$(OBJ_DIR)/$(SIM_BIN) python3 test_p5_highdc_q1_public.py


rtl-byte-owner-check:
	$(MAKE) clean
	$(MAKE) WIDTH=8 HEIGHT=8 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_rtl_byte_ownership.py all

negative-missing-rtl-raw-check:
	$(MAKE) clean
	$(MAKE) WIDTH=8 HEIGHT=8 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_rtl_byte_ownership.py negative-missing-rtl-raw

negative-testbench-repair-check:
	$(MAKE) clean
	$(MAKE) WIDTH=8 HEIGHT=8 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_rtl_byte_ownership.py negative-testbench-repair

negative-decoder-source-check:
	$(MAKE) clean
	$(MAKE) WIDTH=8 HEIGHT=8 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_rtl_byte_ownership.py negative-decoder-source

negative-invalid-shared-oracle-check:
	$(MAKE) clean
	$(MAKE) WIDTH=8 HEIGHT=8 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_rtl_byte_ownership.py negative-invalid-shared-oracle

negative-obu-length-repair-check:
	$(MAKE) clean
	$(MAKE) WIDTH=8 HEIGHT=8 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_rtl_byte_ownership.py negative-obu-length-repair

standalone-matrix-check:
	$(MAKE) clean
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) entropy-check
	$(MAKE) WIDTH=16 HEIGHT=16 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) bitstream-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) inv-xform-check
	$(MAKE) WIDTH=32 HEIGHT=32 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) inter-pred-check
	$(MAKE) WIDTH=32 HEIGHT=32 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) chroma-inter-pred-check
	$(MAKE) WIDTH=32 HEIGHT=32 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) me-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) chroma-residual-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) chroma-coeff-table-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) top-chroma-integration-check

top-public-matrix-check:
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) nonzero-chroma-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) nonzero-chroma16-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural32-chroma-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural32-ip-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural32-ip-newmv-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural32-ip-fullcoeff-newmv-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural64-ip-fullcoeff-newmv-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural64-ip-fullcoeff-newmv-boundary52-probe-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural64-ip-fullcoeff-newmv-3frame-probe-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural64-ip-fullcoeff-newmv-4frame-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural64-ip-fullcoeff-newmv-5frame-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural64-ip-fullcoeff-newmv-6frame-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural32-ip-fractional-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) gop-lifecycle-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural64-ip-mode-context-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural64-ip-fractional-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) p5-highdc-q1-public-check

av1-current-matrix-check av1-regression-matrix:
	cd .. && python3 scripts/run_av1_regression_matrix.py

natural32-ip-chroma-delta-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=32 HEIGHT=32 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural32_ip_chroma_delta_syntax.py

natural64-ip-fractional-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fractional_syntax.py

natural64-ip-mode-context-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_mode_context_syntax.py

clean:
	rm -rf $(OBJ_DIR) $(TRACE_OBJ_DIR) $(ENTROPY_OBJ_DIR) $(BITSTREAM_OBJ_DIR) $(INV_XFORM_OBJ_DIR) $(INTER_OBJ_DIR) $(ME_OBJ_DIR) $(CHROMA_INTER_OBJ_DIR) $(CHROMA_RES_OBJ_DIR) $(SIM_BIN) $(TRACE_SIM_BIN) $(ENTROPY_SIM_BIN) $(BITSTREAM_SIM_BIN) $(INV_XFORM_SIM_BIN) $(INTER_SIM_BIN) $(ME_SIM_BIN) $(CHROMA_INTER_SIM_BIN) $(CHROMA_RES_SIM_BIN) $(CHROMA_COEFF_TABLE_BIN) test_chroma_coeff_tables __pycache__ *.vcd


help:
	@echo "Targets:"
	@echo "  all    (default) Build simulator binary"
	@echo "  run    Build and run (FRAMES=1)"
	@echo "  wave   Build and run with VCD tracing"
	@echo "  entropy-check  Build and run the standalone av1_entropy reference check"
	@echo "  bitstream-check Build and run the standalone av1_bitstream header check"
	@echo "  header-qindex-sweep-check Sweep standalone sequence/frame headers over edge geometries and qindices"
	@echo "  deterministic-packaging-check Verify repeated top-level RTL raw/IVF/recon determinism"
	@echo "  superblock-geometry-syntax-check Sweep generated superblock geometry public-decoder proofs"
	@echo "  qindex-edge-syntax-check Sweep fixed-qindex public-decoder edge cases and qindex=0 clamp"
	@echo "  inv-xform-check Build and run the standalone av1_inverse_transform reference check"
	@echo "  inter-pred-check Build and run the standalone av1_inter_pred reference check"
	@echo "  me-check Build and run the standalone av1_me subpel reference check"
	@echo "  chroma-inter-pred-check Build and run the standalone av1_chroma_inter_pred reference check"
	@echo "  chroma-residual-check Build and run the standalone av1_chroma_residual transform/quant/recon check"
	@echo "  chroma-coeff-table-check Build and run TX_4X4 chroma coefficient table sanity check"
	@echo "  top-chroma-integration-check Static guard for top-level chroma residual plumbing"
	@echo "  natural32-ip-syntax-check 32x32 two-frame zero-MV natural-ish IP public-decoder proof"
	@echo "  natural64-chroma-syntax-check 64x64 sparse flat-luma / non-zero-chroma public-decoder proof"
	@echo "  natural32-ip-newmv-syntax-check 32x32 shifted two-frame NEWMV public-decoder proof"
	@echo "  natural32-ip-fullcoeff-newmv-syntax-check 32x32 full-coeff integer NEWMV public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-syntax-check 64x64 full-coeff unrestricted integer NEWMV public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-boundary52-probe-check 64x64 legacy block52 NEWMV compatibility public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-3frame-probe-check 64x64 3-frame full-coeff NEWMV public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-4frame-syntax-check 64x64 4-frame full-coeff NEWMV LAST-chain public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-5frame-syntax-check 64x64 5-frame full-coeff NEWMV LAST-chain public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-6frame-syntax-check 64x64 6-frame full-coeff NEWMV LAST-chain public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-7frame-syntax-check 64x64 7-frame full-coeff NEWMV LAST-chain public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-8frame-syntax-check 64x64 8-frame full-coeff NEWMV LAST-chain public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-9frame-syntax-check 64x64 9-frame full-coeff NEWMV LAST-chain public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-10frame-syntax-check 64x64 10-frame full-coeff NEWMV LAST-chain public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-11frame-syntax-check 64x64 11-frame full-coeff NEWMV LAST-chain public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-12frame-syntax-check 64x64 12-frame full-coeff NEWMV LAST-chain public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-13frame-syntax-check 64x64 13-frame full-coeff NEWMV LAST-chain public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-14frame-syntax-check 64x64 14-frame full-coeff NEWMV LAST-chain public-decoder proof"
	@echo "  natural64-ip-fullcoeff-newmv-15frame-syntax-check 64x64 15-frame full-coeff NEWMV LAST-chain public-decoder proof"
	@echo "  natural32-ip-fractional-syntax-check 32x32 half-pel two-frame NEWMV public-decoder proof"
	@echo "  gop-lifecycle-syntax-check 64x64 13-frame lowdelay_last GOP lifecycle public-decoder proof"
	@echo "  natural64-ip-mode-context-syntax-check 64x64 LAST-path GLOBALMV/NEARESTMV/NEARMV/NEWMV syntax gate"
	@echo "  natural64-ip-fractional-syntax-check 64x64 half-pel two-frame NEWMV public-decoder proof"
	@echo "  partition-tree-current8x8-check 64x64 RTL-owned split-to-8x8 partition proof"
	@echo "  qindex-sweep-lossy-check qindex 1..255 lossy public-decoder sweep"
	@echo "  skip-txb-cbp-matrix-check all-zero/luma/Cb/Cr/mixed CBP public-decoder proof"
	@echo "  quant-header-disabled-check static guard for qmatrix/delta-q/SB-QP disabled policy"
	@echo "  p5-highdc-q1-public-check 16x16 qindex=1 high-DC / non-zero AC public-decoder proof"
	@echo "  p5-luma-coeff-eob-sweep-check 8x8 reduced short/mid/long luma coefficient EOB public-decoder proof"
	@echo "  rtl-byte-owner-check Positive RTL-byte ownership proof plus N1-N5 anti-repair probes"
	@echo "  standalone-matrix-check Run S1-S9 standalone reduced-subset gates"
	@echo "  top-public-matrix-check Run T1-T11 top-level public-decoder gates"
	@echo "  av1-current-matrix-check Run scripted current boundary matrix and write manifest"
	@echo "  negative-*-check Run individual ownership negative probes"
	@echo "  natural32-ip-chroma-delta-syntax-check 32x32 two-frame zero-MV chroma-inter residual public-decoder proof (64x64 follow-on still TODO)"
	@echo "  natural64-ip-fractional-syntax-check 64x64 half-pel two-frame NEWMV public-decoder proof"
	@echo "  clean  Remove build artifacts"
	@echo ""
	@echo "Variables (override on command line):"
	@echo "  FRAMES       Number of frames to encode  (default: 1)"
	@echo "  TIMEOUT      Max simulation cycles        (default: 500000000)"
	@echo "  QINDEX       Quantization index 0-255     (default: 128)"
	@echo "  YUV_INPUT    Raw YUV420 input file"
	@echo "  OBU_OUTPUT   Encoded AV1 OBU output file"
	@echo "  THREADS      Verilator sim threads        (default: nproc, override with THREADS=16 on Chud PC 2)"
	@echo "  BUILD_JOBS   Parallel build jobs          (default: THREADS, override with BUILD_JOBS=16 on Chud PC 2)"
	@echo "  WIDTH/HEIGHT Resolution                   (default: 1280x720)"
	@echo "  clean  Remove build artifacts"
	@echo ""
	@echo "Variables (override on command line):"
	@echo "  FRAMES       Number of frames to encode  (default: 1)"
	@echo "  TIMEOUT      Max simulation cycles        (default: 500000000)"
	@echo "  QINDEX       Quantization index 0-255     (default: 128)"
	@echo "  YUV_INPUT    Raw YUV420 input file"
	@echo "  OBU_OUTPUT   Encoded AV1 OBU output file"
	@echo "  THREADS      Verilator sim threads        (default: nproc, override with THREADS=16 on Chud PC 2)"
	@echo "  BUILD_JOBS   Parallel build jobs          (default: THREADS, override with BUILD_JOBS=16 on Chud PC 2)"
	@echo "  WIDTH/HEIGHT Resolution                   (default: 1280x720)"
