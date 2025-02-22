#!/bin/bash -l
#SBATCH --export=ALL
#SBATCH --partition=commons        # or another valid partition
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem-per-cpu=512
#SBATCH --threads-per-core=1
#SBATCH --time=00:30:00
#SBATCH --job-name=OthelloTest
#SBATCH --output=othello_test_%j.out
# If you do not actually have a reservation named 'classroom', comment out:
# #SBATCH --reservation=classroom

# Load any required modules for your environment
module load assignment1
module list

echo "===== DEBUG INFO ====="
echo "Current directory: $(pwd)"
echo "Files in current directory:"
ls -lh
echo "======================"
echo ""

# Optional quick single test with 2 workers (no loop)
echo "Running a quick single test with CILK_NWORKERS=2"
CILK_NWORKERS=2 ./othello < default_input

echo ""
echo "===== LOOPING FROM 1..2 WORKERS ====="

for ((i = 1; i <= 10; i++)); do
    echo "CILK_NWORKERS=$i"
    
    # Use Bash's built-in time in a subshell
    CILK_NWORKERS=$i bash -c "time -p ./othello < default_input"
    
    echo "Finished iteration $i"
    echo ""
done

echo "===== ALL RUNS COMPLETE ====="
