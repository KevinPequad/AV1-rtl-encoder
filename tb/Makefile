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

.PHONY: all run wave entropy-check bitstream-check header-qindex-sweep-check deterministic-packaging-check superblock-geometry-syntax-check qindex-edge-syntax-check inv-xform-check inter-pred-check me-check chroma-inter-pred-check chroma-residual-check chroma-coeff-table-check top-chroma-integration-check nonzero-chroma-syntax-check nonzero-chroma16-syntax-check natural32-chroma-syntax-check natural64-chroma-syntax-check natural32-ip-syntax-check natural32-ip-newmv-syntax-check natural32-ip-fullcoeff-newmv-syntax-check natural64-ip-fullcoeff-newmv-syntax-check natural64-ip-fullcoeff-newmv-boundary45-probe-check natural64x128-ip-fullcoeff-newmv-syntax-check natural128x64-ip-fullcoeff-newmv-syntax-check natural128x128-ip-fullcoeff-newmv-syntax-check natural192x128-ip-fullcoeff-newmv-syntax-check natural256x128-ip-fullcoeff-newmv-syntax-check natural320x128-ip-fullcoeff-newmv-syntax-check natural384x128-ip-fullcoeff-newmv-syntax-check natural448x128-ip-fullcoeff-newmv-syntax-check natural512x128-ip-fullcoeff-newmv-syntax-check natural576x128-ip-fullcoeff-newmv-syntax-check natural640x128-ip-fullcoeff-newmv-syntax-check natural704x128-ip-fullcoeff-newmv-syntax-check natural768x128-ip-fullcoeff-newmv-syntax-check natural832x128-ip-fullcoeff-newmv-syntax-check natural896x128-ip-fullcoeff-newmv-syntax-check natural960x128-ip-fullcoeff-newmv-syntax-check natural1024x128-ip-fullcoeff-newmv-syntax-check natural1088x128-ip-fullcoeff-newmv-syntax-check natural1152x128-ip-fullcoeff-newmv-syntax-check natural1216x128-ip-fullcoeff-newmv-syntax-check natural1280x128-ip-fullcoeff-newmv-syntax-check natural1344x128-ip-fullcoeff-newmv-syntax-check natural1408x128-ip-fullcoeff-newmv-syntax-check natural1472x128-ip-fullcoeff-newmv-syntax-check natural1536x128-ip-fullcoeff-newmv-syntax-check natural1600x128-ip-fullcoeff-newmv-syntax-check natural192x64-ip-fullcoeff-newmv-syntax-check natural256x64-ip-fullcoeff-newmv-syntax-check natural320x64-ip-fullcoeff-newmv-syntax-check natural384x64-ip-fullcoeff-newmv-syntax-check natural448x64-ip-fullcoeff-newmv-syntax-check natural512x64-ip-fullcoeff-newmv-syntax-check natural576x64-ip-fullcoeff-newmv-syntax-check natural640x64-ip-fullcoeff-newmv-syntax-check natural704x64-ip-fullcoeff-newmv-syntax-check natural768x64-ip-fullcoeff-newmv-syntax-check natural832x64-ip-fullcoeff-newmv-syntax-check natural896x64-ip-fullcoeff-newmv-syntax-check natural960x64-ip-fullcoeff-newmv-syntax-check natural1024x64-ip-fullcoeff-newmv-syntax-check natural1088x64-ip-fullcoeff-newmv-syntax-check natural1152x64-ip-fullcoeff-newmv-syntax-check natural1216x64-ip-fullcoeff-newmv-syntax-check natural1280x64-ip-fullcoeff-newmv-syntax-check natural1344x64-ip-fullcoeff-newmv-syntax-check natural1408x64-ip-fullcoeff-newmv-syntax-check natural1472x64-ip-fullcoeff-newmv-syntax-check natural1536x64-ip-fullcoeff-newmv-syntax-check natural1600x64-ip-fullcoeff-newmv-syntax-check natural1664x64-ip-fullcoeff-newmv-syntax-check natural1728x64-ip-fullcoeff-newmv-syntax-check natural1792x64-ip-fullcoeff-newmv-syntax-check natural1856x64-ip-fullcoeff-newmv-syntax-check natural1920x64-ip-fullcoeff-newmv-syntax-check natural1984x64-ip-fullcoeff-newmv-syntax-check natural32-ip-fractional-syntax-check natural32-ip-chroma-delta-syntax-check natural64-ip-fractional-syntax-check natural64-ip-mode-context-syntax-check gop-lifecycle-syntax-check rtl-byte-owner-check negative-missing-rtl-raw-check negative-testbench-repair-check negative-decoder-source-check negative-invalid-shared-oracle-check negative-obu-length-repair-check standalone-matrix-check top-public-matrix-check av1-current-matrix-check av1-regression-matrix partition-tree-current8x8-check qindex-sweep-lossy-check skip-txb-cbp-matrix-check quant-header-disabled-check p5-highdc-q1-public-check clean help

all: $(SIM_BIN)

$(SIM_BIN): $(TB_CPP) $(RTL_SRCS)
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

natural64x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64x128_ip_fullcoeff_newmv_syntax.py

natural128x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=128 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural128x64_ip_fullcoeff_newmv_syntax.py

natural128x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=128 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural128x128_ip_fullcoeff_newmv_syntax.py

natural192x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=192 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural192x128_ip_fullcoeff_newmv_syntax.py

natural256x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=256 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural256x128_ip_fullcoeff_newmv_syntax.py

natural320x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=320 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural320x128_ip_fullcoeff_newmv_syntax.py


natural384x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=384 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural384x128_ip_fullcoeff_newmv_syntax.py


natural448x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=448 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural448x128_ip_fullcoeff_newmv_syntax.py


natural512x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=512 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural512x128_ip_fullcoeff_newmv_syntax.py

natural576x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=576 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural576x128_ip_fullcoeff_newmv_syntax.py

natural640x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=640 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural640x128_ip_fullcoeff_newmv_syntax.py

natural704x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=704 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural704x128_ip_fullcoeff_newmv_syntax.py

natural768x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=768 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural768x128_ip_fullcoeff_newmv_syntax.py

natural832x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=832 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural832x128_ip_fullcoeff_newmv_syntax.py


natural896x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=896 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural896x128_ip_fullcoeff_newmv_syntax.py


natural960x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=960 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural960x128_ip_fullcoeff_newmv_syntax.py


natural1024x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1024 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1024x128_ip_fullcoeff_newmv_syntax.py

natural1088x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1088 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1088x128_ip_fullcoeff_newmv_syntax.py

natural1152x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1152 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1152x128_ip_fullcoeff_newmv_syntax.py

natural1216x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1216 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1216x128_ip_fullcoeff_newmv_syntax.py


natural1280x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1280 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1280x128_ip_fullcoeff_newmv_syntax.py


natural1344x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1344 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1344x128_ip_fullcoeff_newmv_syntax.py


natural1408x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1408 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1408x128_ip_fullcoeff_newmv_syntax.py


natural1472x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1472 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1472x128_ip_fullcoeff_newmv_syntax.py


natural1536x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1536 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1536x128_ip_fullcoeff_newmv_syntax.py


natural1600x128-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1600 HEIGHT=128 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1600x128_ip_fullcoeff_newmv_syntax.py

natural192x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=192 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural192x64_ip_fullcoeff_newmv_syntax.py

natural256x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=256 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural256x64_ip_fullcoeff_newmv_syntax.py

natural320x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=320 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural320x64_ip_fullcoeff_newmv_syntax.py

natural384x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=384 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural384x64_ip_fullcoeff_newmv_syntax.py

natural448x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=448 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural448x64_ip_fullcoeff_newmv_syntax.py

natural512x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=512 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural512x64_ip_fullcoeff_newmv_syntax.py

natural576x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=576 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural576x64_ip_fullcoeff_newmv_syntax.py

natural640x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=640 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural640x64_ip_fullcoeff_newmv_syntax.py

natural704x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=704 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural704x64_ip_fullcoeff_newmv_syntax.py

natural768x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=768 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural768x64_ip_fullcoeff_newmv_syntax.py

natural832x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=832 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural832x64_ip_fullcoeff_newmv_syntax.py

natural896x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=896 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural896x64_ip_fullcoeff_newmv_syntax.py


natural960x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=960 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural960x64_ip_fullcoeff_newmv_syntax.py


natural1024x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1024 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1024x64_ip_fullcoeff_newmv_syntax.py


natural1088x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1088 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1088x64_ip_fullcoeff_newmv_syntax.py

natural1152x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1152 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1152x64_ip_fullcoeff_newmv_syntax.py


natural1216x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1216 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1216x64_ip_fullcoeff_newmv_syntax.py


natural1280x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1280 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1280x64_ip_fullcoeff_newmv_syntax.py


natural1344x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1344 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1344x64_ip_fullcoeff_newmv_syntax.py


natural1408x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1408 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1408x64_ip_fullcoeff_newmv_syntax.py


natural1472x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1472 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1472x64_ip_fullcoeff_newmv_syntax.py


natural1536x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1536 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1536x64_ip_fullcoeff_newmv_syntax.py


natural1600x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1600 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1600x64_ip_fullcoeff_newmv_syntax.py


natural1664x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1664 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1664x64_ip_fullcoeff_newmv_syntax.py


natural1728x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1728 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1728x64_ip_fullcoeff_newmv_syntax.py


natural1792x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1792 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1792x64_ip_fullcoeff_newmv_syntax.py


natural1856x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1856 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1856x64_ip_fullcoeff_newmv_syntax.py


natural1920x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1920 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1920x64_ip_fullcoeff_newmv_syntax.py


natural1984x64-ip-fullcoeff-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=1984 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural1984x64_ip_fullcoeff_newmv_syntax.py


natural64-ip-fullcoeff-newmv-boundary45-probe-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fullcoeff_newmv_boundary45_probe.py

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
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural64x128-ip-fullcoeff-newmv-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural128x64-ip-fullcoeff-newmv-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural128x128-ip-fullcoeff-newmv-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural192x128-ip-fullcoeff-newmv-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural256x128-ip-fullcoeff-newmv-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural320x128-ip-fullcoeff-newmv-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural384x128-ip-fullcoeff-newmv-syntax-check
	$(MAKE) THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) natural192x64-ip-fullcoeff-newmv-syntax-check
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
	@echo "  natural64x128-ip-fullcoeff-newmv-syntax-check 64x128 full-coeff unrestricted integer NEWMV public-decoder proof"
	@echo "  natural128x64-ip-fullcoeff-newmv-syntax-check 128x64 full-coeff unrestricted integer NEWMV public-decoder proof"
	@echo "  natural128x128-ip-fullcoeff-newmv-syntax-check 128x128 unrestricted full-coeff NEWMV public-decoder proof"
	@echo "  natural192x128-ip-fullcoeff-newmv-syntax-check 192x128 unrestricted full-coeff NEWMV public-decoder proof"
	@echo "  natural256x128-ip-fullcoeff-newmv-syntax-check 256x128 unrestricted full-coeff NEWMV public-decoder proof"
	@echo "  natural320x128-ip-fullcoeff-newmv-syntax-check 320x128 unrestricted full-coeff NEWMV public-decoder proof"
	@echo "  natural384x128-ip-fullcoeff-newmv-syntax-check 384x128 unrestricted full-coeff NEWMV public-decoder proof"
	@echo "  natural448x128..natural1600x128-ip-fullcoeff-newmv-syntax-check wider 128-high unrestricted full-coeff NEWMV proofs"
	@echo "  natural192x64-ip-fullcoeff-newmv-syntax-check 192x64 full-coeff unrestricted integer NEWMV public-decoder proof"
	@echo "  natural256x64-ip-fullcoeff-newmv-syntax-check 256x64 full-coeff unrestricted integer NEWMV public-decoder proof"
	@echo "  natural320x64-ip-fullcoeff-newmv-syntax-check 320x64 full-coeff unrestricted integer NEWMV public-decoder proof"
	@echo "  natural384x64-ip-fullcoeff-newmv-syntax-check 384x64 full-coeff unrestricted integer NEWMV public-decoder proof"
	@echo "  natural448x64..natural1984x64-ip-fullcoeff-newmv-syntax-check wider 64-high full-coeff unrestricted integer NEWMV proofs"
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
