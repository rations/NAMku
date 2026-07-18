# nambench — standalone NAM core throughput benchmark.
#
# Builds the NAM DSP core sources directly (no cmake, no plug-in SDK) so the
# same command works on Haiku and on Linux and the two results are comparable.
#
#   make                 baseline: double precision, no -march  (matches how
#                                  NAMku, the Linux port and upstream all ship)
#   make avx             + -mavx            (Sandy Bridge 2011 and newer)
#   make float           + float precision  (portable, any CPU)
#   make fast            + both
#   make all-variants    build all four side by side for A/B timing
#
# Point DEPS at a tree containing NeuralAmpModelerCore, AudioDSPTools and
# eigen. Defaults to NAMku's own deps/; on the Linux port use:
#   make DEPS=$(HOME)/gateway-linux
#
# The Linux port keeps eigen and the cores at the top level, so DEPS works for
# both layouts unchanged.

DEPS ?= ../deps

CORE  := $(DEPS)/NeuralAmpModelerCore
TOOLS := $(DEPS)/AudioDSPTools
EIGEN := $(DEPS)/eigen

SRC := nambench.cpp \
       $(CORE)/NAM/activations.cpp \
       $(CORE)/NAM/container.cpp \
       $(CORE)/NAM/conv1d.cpp \
       $(CORE)/NAM/convnet.cpp \
       $(CORE)/NAM/dsp.cpp \
       $(CORE)/NAM/get_dsp.cpp \
       $(CORE)/NAM/lstm.cpp \
       $(CORE)/NAM/ring_buffer.cpp \
       $(CORE)/NAM/util.cpp \
       $(CORE)/NAM/wavenet/model.cpp \
       $(CORE)/NAM/wavenet/a2_fast.cpp \
       $(CORE)/NAM/wavenet/slimmable.cpp \
       $(TOOLS)/dsp/dsp.cpp

INC := -I$(CORE) -I$(CORE)/NAM -I$(CORE)/Dependencies/nlohmann \
       -I$(TOOLS)/dsp -I$(EIGEN)

# Baseline flags, identical to how all three plug-ins are built today:
# -O3 -DNDEBUG -ffast-math, double precision, no -march.
BASE := -std=c++20 -O3 -DNDEBUG -ffast-math -DNAM_ENABLE_A2_FAST -Wno-unused-parameter

# Both precision defines must flip together: NAM and AudioDSPTools carry
# separate switches and buffers cross that boundary, so setting one alone is a
# compile error rather than a silent mismatch.
FLOATDEFS := -DNAM_SAMPLE_FLOAT -DDSP_SAMPLE_FLOAT

.PHONY: all avx float fast all-variants clean

all: nambench

nambench: $(SRC)
	$(CXX) $(BASE) $(INC) $(SRC) -o $@

avx: nambench-avx
nambench-avx: $(SRC)
	$(CXX) $(BASE) -mavx $(INC) $(SRC) -o $@

float: nambench-float
nambench-float: $(SRC)
	$(CXX) $(BASE) $(FLOATDEFS) $(INC) $(SRC) -o $@

fast: nambench-fast
nambench-fast: $(SRC)
	$(CXX) $(BASE) -mavx $(FLOATDEFS) $(INC) $(SRC) -o $@

all-variants: nambench nambench-avx nambench-float nambench-fast

clean:
	rm -f nambench nambench-avx nambench-float nambench-fast
