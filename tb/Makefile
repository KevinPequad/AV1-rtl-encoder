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
TOP_MODULE      := av1_encoder_top
TB_CPP          := tb_av1_encoder.cpp
RTL_DIR         := ../rtl
OBJ_DIR         := obj_dir
SIM_BIN         := V$(TOP_MODULE)

# Resolution (override on command line for different sizes)
WIDTH           ?= 1280
HEIGHT          ?= 720

# Collect all Verilog sources from the RTL directory
RTL_SRCS        := $(wildcard $(RTL_DIR)/*.v)

# Verilator flags
VERILATOR_FLAGS := --cc --exe --build \
    -Wall -Wno-fatal \
    --trace \
    --top-module $(TOP_MODULE) \
    -I$(RTL_DIR) \
    -GFRAME_WIDTH=$(WIDTH) -GFRAME_HEIGHT=$(HEIGHT) \
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

.PHONY: all run wave clean help

all: $(SIM_BIN)

$(SIM_BIN): $(TB_CPP) $(RTL_SRCS)
	$(VERILATOR) $(VERILATOR_FLAGS) \
		$(RTL_SRCS) \
		$(TB_CPP) \
		-o $(SIM_BIN)
	@cp -f $(OBJ_DIR)/$(SIM_BIN) ./$(SIM_BIN) 2>/dev/null || true
	@echo ""
	@echo "Build complete: ./$(SIM_BIN)"

run: $(SIM_BIN)
	@mkdir -p $(dir $(OBU_OUTPUT))
	./$(SIM_BIN) \
		+frames=$(FRAMES) \
		+timeout=$(TIMEOUT) \
		+qindex=$(QINDEX) \
		+input=$(YUV_INPUT) \
		+output=$(OBU_OUTPUT) \
		$(EXTRA_PLUSARGS)

wave: $(SIM_BIN)
	@mkdir -p $(dir $(OBU_OUTPUT))
	./$(SIM_BIN) \
		+frames=$(FRAMES) \
		+timeout=$(TIMEOUT) \
		+qindex=$(QINDEX) \
		+input=$(YUV_INPUT) \
		+output=$(OBU_OUTPUT) \
		+trace \
		$(EXTRA_PLUSARGS)

clean:
	rm -rf $(OBJ_DIR) $(SIM_BIN) *.vcd

help:
	@echo "Targets:"
	@echo "  all    (default) Build simulator binary"
	@echo "  run    Build and run (FRAMES=1)"
	@echo "  wave   Build and run with VCD tracing"
	@echo "  clean  Remove build artifacts"
	@echo ""
	@echo "Variables (override on command line):"
	@echo "  FRAMES       Number of frames to encode  (default: 1)"
	@echo "  TIMEOUT      Max simulation cycles        (default: 500000000)"
	@echo "  QINDEX       Quantization index 0-255     (default: 128)"
	@echo "  YUV_INPUT    Raw YUV420 input file"
	@echo "  OBU_OUTPUT   Encoded AV1 OBU output file"
	@echo "  WIDTH/HEIGHT Resolution                   (default: 1280x720)"
