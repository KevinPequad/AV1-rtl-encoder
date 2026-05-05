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

# Collect all Verilog sources from the RTL directory
RTL_SRCS        := $(wildcard $(RTL_DIR)/*.v)

# Verilator flags
VERILATOR_COMMON_FLAGS := --cc --exe \
    -Wall -Wno-fatal \
    --build-jobs $(BUILD_JOBS) \
    --threads $(THREADS) \
    --top-module $(TOP_MODULE) \
    -I$(RTL_DIR) \
    -GFRAME_WIDTH=$(WIDTH) -GFRAME_HEIGHT=$(HEIGHT)

VERILATOR_FLAGS := $(VERILATOR_COMMON_FLAGS) \
    -Mdir $(OBJ_DIR) \
    -CFLAGS "-O2 -std=c++17 -DFRAME_W=$(WIDTH) -DFRAME_H=$(HEIGHT)"

VERILATOR_TRACE_FLAGS := $(VERILATOR_COMMON_FLAGS) \
    --trace \
    -Mdir $(TRACE_OBJ_DIR) \
    -CFLAGS "-O2 -std=c++17 -DFRAME_W=$(WIDTH) -DFRAME_H=$(HEIGHT)"

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

.PHONY: all run wave entropy-check bitstream-check inv-xform-check inter-pred-check me-check chroma-inter-pred-check chroma-residual-check chroma-coeff-table-check top-chroma-integration-check nonzero-chroma-syntax-check nonzero-chroma16-syntax-check natural32-chroma-syntax-check natural32-ip-syntax-check natural32-ip-newmv-syntax-check natural32-ip-fractional-syntax-check natural64-ip-fractional-syntax-check clean help

all: $(SIM_BIN)

$(SIM_BIN): $(TB_CPP) $(RTL_SRCS)
	$(VERILATOR) $(VERILATOR_FLAGS) \
		$(RTL_SRCS) \
		$(TB_CPP) \
		-o $(SIM_BIN)
	$(MAKE) -C $(OBJ_DIR) -f V$(TOP_MODULE).mk -j $(BUILD_JOBS) AR="$(AR)" CXX="$(CXX)" LINK="$(LINK)"
	@cp -f $(OBJ_DIR)/$(SIM_BIN) ./$(SIM_BIN) 2>/dev/null || true
	@cp -f $(OBJ_DIR)/$(SIM_BIN) ./Vav1_encoder_top 2>/dev/null || true
	@echo ""
	@echo "Build complete: ./$(SIM_BIN)"

$(TRACE_SIM_BIN): $(TB_CPP) $(RTL_SRCS)
	$(VERILATOR) $(VERILATOR_TRACE_FLAGS) \
		$(RTL_SRCS) \
		$(TB_CPP) \
		-o $(TRACE_SIM_BIN)
	$(MAKE) -C $(TRACE_OBJ_DIR) -f V$(TOP_MODULE).mk -j $(BUILD_JOBS) AR="$(AR)" CXX="$(CXX)" LINK="$(LINK)"
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
		-CFLAGS "-O2 -std=c++17 -DFRAME_W=$(WIDTH) -DFRAME_H=$(HEIGHT)" \
		$(RTL_DIR)/av1_bitstream.v \
		$(BITSTREAM_TB_CPP) \
		-o $(BITSTREAM_SIM_BIN)
	$(MAKE) -C $(BITSTREAM_OBJ_DIR) -f V$(BITSTREAM_TOP).mk -j $(BUILD_JOBS) AR="$(AR)" CXX="$(CXX)" LINK="$(LINK)"
	@cp -f $(BITSTREAM_OBJ_DIR)/$(BITSTREAM_SIM_BIN) ./$(BITSTREAM_SIM_BIN) 2>/dev/null || true
	@echo ""
	@echo "Build complete: ./$(BITSTREAM_SIM_BIN)"



$(ME_SIM_BIN): $(ME_TB_CPP) $(RTL_DIR)/av1_me.v $(RTL_DIR)/av1_inter_pred.v
	$(VERILATOR) --cc --exe 		-Wall -Wno-fatal 		--build-jobs $(BUILD_JOBS) 		--threads $(THREADS) 		--top-module $(ME_TOP) 		-I$(RTL_DIR) 		-GFRAME_WIDTH=$(WIDTH) -GFRAME_HEIGHT=$(HEIGHT) -GSEARCH_RANGE=2 		-Mdir $(ME_OBJ_DIR) 		-CFLAGS "-O2 -std=c++17 -DFRAME_W=$(WIDTH) -DFRAME_H=$(HEIGHT)" 		$(RTL_DIR)/av1_inter_pred.v 		$(RTL_DIR)/av1_me.v 		$(ME_TB_CPP) 		-o $(ME_SIM_BIN)
	$(MAKE) -C $(ME_OBJ_DIR) -f V$(ME_TOP).mk -j $(BUILD_JOBS) AR="$(AR)" CXX="$(CXX)" LINK="$(LINK)"
	@cp -f $(ME_OBJ_DIR)/$(ME_SIM_BIN) ./$(ME_SIM_BIN) 2>/dev/null || true
	@echo ""
	@echo "Build complete: ./$(ME_SIM_BIN)"

$(INTER_SIM_BIN): $(INTER_TB_CPP) $(RTL_DIR)/av1_inter_pred.v
	$(VERILATOR) --cc --exe 		-Wall -Wno-fatal 		--build-jobs $(BUILD_JOBS) 		--threads $(THREADS) 		--top-module $(INTER_TOP) 		-I$(RTL_DIR) 		-GFRAME_WIDTH=$(WIDTH) -GFRAME_HEIGHT=$(HEIGHT) 		-Mdir $(INTER_OBJ_DIR) 		-CFLAGS "-O2 -std=c++17 -DFRAME_W=$(WIDTH) -DFRAME_H=$(HEIGHT)" 		$(RTL_DIR)/av1_inter_pred.v 		$(INTER_TB_CPP) 		-o $(INTER_SIM_BIN)
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

inv-xform-check: $(INV_XFORM_SIM_BIN)
	./$(INV_XFORM_SIM_BIN)

inter-pred-check: $(INTER_SIM_BIN)
	./$(INTER_SIM_BIN)


$(CHROMA_INTER_SIM_BIN): $(CHROMA_INTER_TB_CPP) $(RTL_DIR)/av1_chroma_inter_pred.v
	$(VERILATOR) --cc --exe 		-Wall -Wno-fatal 		--build-jobs $(BUILD_JOBS) 		--threads $(THREADS) 		--top-module $(CHROMA_INTER_TOP) 		-I$(RTL_DIR) 		-GCHROMA_WIDTH=$(shell expr $(WIDTH) / 2) -GCHROMA_HEIGHT=$(shell expr $(HEIGHT) / 2) 		-Mdir $(CHROMA_INTER_OBJ_DIR) 		-CFLAGS "-O2 -std=c++17 -DFRAME_W=$(WIDTH) -DFRAME_H=$(HEIGHT)" 		$(RTL_DIR)/av1_chroma_inter_pred.v 		$(CHROMA_INTER_TB_CPP) 		-o $(CHROMA_INTER_SIM_BIN)
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
	$(CXX) -O2 -std=c++17 $(CHROMA_COEFF_TABLE_TB_CPP) -o test_chroma_coeff_tables
	./test_chroma_coeff_tables

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

natural32-ip-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=32 HEIGHT=32 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural32_ip_syntax.py

natural32-ip-newmv-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=32 HEIGHT=32 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural32_ip_newmv_syntax.py

natural32-ip-fractional-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=32 HEIGHT=32 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural32_ip_fractional_syntax.py

natural64-ip-fractional-syntax-check:
	$(MAKE) clean
	$(MAKE) WIDTH=64 HEIGHT=64 THREADS=$(THREADS) BUILD_JOBS=$(BUILD_JOBS) all
	python3 test_natural64_ip_fractional_syntax.py

clean:
	rm -rf $(OBJ_DIR) $(TRACE_OBJ_DIR) $(ENTROPY_OBJ_DIR) $(BITSTREAM_OBJ_DIR) $(INV_XFORM_OBJ_DIR) $(INTER_OBJ_DIR) $(ME_OBJ_DIR) $(CHROMA_INTER_OBJ_DIR) $(CHROMA_RES_OBJ_DIR) $(SIM_BIN) $(TRACE_SIM_BIN) $(ENTROPY_SIM_BIN) $(BITSTREAM_SIM_BIN) $(INV_XFORM_SIM_BIN) $(INTER_SIM_BIN) $(ME_SIM_BIN) $(CHROMA_INTER_SIM_BIN) $(CHROMA_RES_SIM_BIN) test_chroma_coeff_tables *.vcd

help:
	@echo "Targets:"
	@echo "  all    (default) Build simulator binary"
	@echo "  run    Build and run (FRAMES=1)"
	@echo "  wave   Build and run with VCD tracing"
	@echo "  entropy-check  Build and run the standalone av1_entropy reference check"
	@echo "  bitstream-check Build and run the standalone av1_bitstream header check"
	@echo "  inv-xform-check Build and run the standalone av1_inverse_transform reference check"
	@echo "  inter-pred-check Build and run the standalone av1_inter_pred reference check"
	@echo "  me-check Build and run the standalone av1_me subpel reference check"
	@echo "  chroma-inter-pred-check Build and run the standalone av1_chroma_inter_pred reference check"
	@echo "  chroma-residual-check Build and run the standalone av1_chroma_residual transform/quant/recon check"
	@echo "  chroma-coeff-table-check Build and run TX_4X4 chroma coefficient table sanity check"
	@echo "  top-chroma-integration-check Static guard for top-level chroma residual plumbing"
	@echo "  natural32-ip-syntax-check 32x32 two-frame zero-MV natural-ish IP public-decoder proof"
	@echo "  natural32-ip-newmv-syntax-check 32x32 shifted two-frame NEWMV public-decoder proof"
	@echo "  natural32-ip-fractional-syntax-check 32x32 half-pel two-frame NEWMV public-decoder proof"
	@echo "  natural64-ip-fractional-syntax-check 64x64 half-pel two-frame NEWMV public-decoder proof"
	@echo "  clean  Remove build artifacts"
	@echo ""
	@echo "Variables (override on command line):"
	@echo "  FRAMES       Number of frames to encode  (default: 1)"
	@echo "  TIMEOUT      Max simulation cycles        (default: 500000000)"
	@echo "  QINDEX       Quantization index 0-255     (default: 128)"
	@echo "  YUV_INPUT    Raw YUV420 input file"
	@echo "  OBU_OUTPUT   Encoded AV1 OBU output file"
	@echo "  THREADS      Verilator sim threads        (default: 24)"
	@echo "  BUILD_JOBS   Parallel build jobs          (default: 24)"
	@echo "  WIDTH/HEIGHT Resolution                   (default: 1280x720)"
