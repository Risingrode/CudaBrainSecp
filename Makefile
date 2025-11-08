
SRC = CudaBrainSecp.cpp \
      CPU/Point.cpp \
      CPU/Int.cpp \
      CPU/IntMod.cpp \
      CPU/SECP256K1.cpp

OBJDIR = obj

OBJET = $(addprefix $(OBJDIR)/, \
		GPU/GPUSecp.o \
		CPU/Point.o \
		CPU/Int.o \
		CPU/IntMod.o \
		CPU/SECP256K1.o \
        CPU/BIP39.o \
        CudaBrainSecp.o \
)

# Target GPU architectures (space-separated SM versions).
# Default supports Tesla T4 (sm_75) and RTX 30-series (sm_86).
SMS      ?= 75 86
CUDA	  = /usr/local/cuda-11.7
CXX       = g++
CXXCUDA   = /usr/bin/g++
# Enable C++17 for std::filesystem and related features
CXXFLAGS  = -DWITHGPU -m64 -mssse3 -Wno-write-strings -O3 -march=native -std=c++17 -fopenmp -I. -I$(CUDA)/include
LFLAGS    = -lgmp -lpthread -fopenmp -L$(CUDA)/lib64 -lcudart
NVCC      = $(CUDA)/bin/nvcc

# Compose -gencode flags from SMS
NVCC_GENCODE := $(foreach sm,$(SMS),-gencode=arch=compute_$(sm),code=sm_$(sm))

#--------------------------------------------------------------------

$(OBJDIR)/GPU/GPUSecp.o: GPU/GPUSecp.cu
	$(NVCC) --compile --compiler-options -fPIC -ccbin $(CXXCUDA) -m64 -O2 -I$(CUDA)/include $(NVCC_GENCODE) -o $(OBJDIR)/GPU/GPUSecp.o -c GPU/GPUSecp.cu

$(OBJDIR)/%.o : %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<
	
$(OBJDIR)/CPU/%.o : %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

all: CudaBrainSecp

CudaBrainSecp: $(OBJET)
	@echo Making CudaBrainSecp...
	$(CXX) $(OBJET) $(LFLAGS) -o CudaBrainSecp

$(OBJET): | $(OBJDIR) $(OBJDIR)/GPU $(OBJDIR)/CPU

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/GPU: $(OBJDIR)
	cd $(OBJDIR) &&	mkdir -p GPU
	
$(OBJDIR)/CPU: $(OBJDIR)
	cd $(OBJDIR) &&	mkdir -p CPU

clean:
	@echo Cleaning...
	@rm -rf obj || true
