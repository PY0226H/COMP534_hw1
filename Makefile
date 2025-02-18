# Choose the C++ compiler (Intel C++) and add flags
CXX       = icpc
CILK_FLAGS= -std=c++11

# Suppress certain warnings
NOWARN    = -wd3946 -wd3947 -wd10010

EXEC      = othello

# We will produce these targets:
OBJ       = $(EXEC) $(EXEC)-debug $(EXEC)-serial

# Common compilation flags:
OPT       = -O2 -g $(NOWARN) $(CILK_FLAGS)
DEBUG     = -O0 -g $(NOWARN) $(CILK_FLAGS)

# If the user specifies "make runp W=n", we set CILK_NWORKERS=n
ifneq ($(W),)
XX        = CILK_NWORKERS=$(W)
endif

# Default input file
I         = default_input

# ------------------------------------------------------------------------
# Build targets
# ------------------------------------------------------------------------
all: $(OBJ)

# Debug parallel build
$(EXEC)-debug: $(EXEC).cpp
	$(CXX) $(DEBUG) -o $(EXEC)-debug $(EXEC).cpp -lrt

# Serial build (no Cilk runtime overhead)
$(EXEC)-serial: $(EXEC).cpp
	$(CXX) $(OPT) -cilk-serialize -o $(EXEC)-serial $(EXEC).cpp -lrt

# Optimized parallel build
$(EXEC): $(EXEC).cpp
	$(CXX) $(OPT) -o $(EXEC) $(EXEC).cpp -lrt

# ------------------------------------------------------------------------
# Run targets
# ------------------------------------------------------------------------

# Run the optimized parallel version
# Usage: make runp W=4 I=some_input_file
runp: $(EXEC)
	@echo "Running parallel version: workers=$(W), input=$(I)"
	$(XX) ./$(EXEC) < $(I)

# Run the serial version
# Usage: make runs I=some_input_file
runs: $(EXEC)-serial
	@echo "Running serial version with input=$(I)"
	./$(EXEC)-serial < $(I)

# Run with cilkscreen (data race detection)
# Usage: make screen I=some_input_file
screen: $(EXEC)
	@echo "Running under cilkscreen using input=screen_input"
	cilkscreen ./$(EXEC) < screen_input

# Run with cilkview (performance profiling)
# Usage: make view I=some_input_file
view: $(EXEC)
	@echo "Running under cilkview using input=$(I)"
	cilkview ./$(EXEC) < $(I)

# ------------------------------------------------------------------------
# Cleanup
# ------------------------------------------------------------------------
clean:
	/bin/rm -f $(OBJ) 
