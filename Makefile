EXE = convex_visualizer
OBJECTS = convex_visualizer.o

# Compiler flags
CFLAGS = -g # Add any compiler flags you need, like -Wall for all warnings, -g for debugging info

# Linker flags
LDFLAGS = -lncurses

all: $(EXE)

convex_visualizer : convex_visualizer.cpp
	g++ $(CFLAGS) convex_visualizer.cpp -o convex_visualizer $(LDFLAGS) && rm -f convex_visualizer.o

.PHONY : clean

clean :
	rm -f $(EXE) $(SRC:.c=.o) $(OBJECTS)
