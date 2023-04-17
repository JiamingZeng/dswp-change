#!/bin/bash
# Run script for Homework 2 EECS 583 Winter 2023

# ACTION NEEDED: If the path is different, please update it here.
PATH2LIB=~/HW2/build/HW2/LLVMHW2.so        # Specify your build directory in the project

# ACTION NEEDED: Choose the correct pass when running.
PASS=-dswp                   # Choose either -fplicm-correctness ...
# PASS=-fplicm-performance                 # ... or -fplicm-performance

# Delete outputs from previous runs. Update this when you want to retain some files.
rm -f default.profraw *_prof *_fplicm *.bc *.profdata *_output *.ll

# Convert source code to bitcode (IR).
clang -emit-llvm -c ${1}.c -o ${1}.bc

# Canonicalize natural loops (Ref: llvm.org/doxygen/LoopSimplify_8h_source.html)
opt -passes='loop-simplify' ${1}.bc -o ${1}.ls.bc

# Instrument profiler passes.
opt -passes='pgo-instr-gen,instrprof' ${1}.ls.bc -o ${1}.ls.prof.bc

# Note: We are using the New Pass Manager for these passes! 

# Generate binary executable with profiler embedded
clang -fprofile-instr-generate ${1}.ls.prof.bc -o ${1}_prof

# When we run the profiler embedded executable, it generates a default.profraw file that contains the profile data.
./${1}_prof > correct_output

# Converting it to LLVM form. This step can also be used to combine multiple profraw files,
# in case you want to include different profile runs together.
llvm-profdata merge -o ${1}.profdata default.profraw

# The "Profile Guided Optimization Use" pass attaches the profile data to the bc file.
opt -passes="pgo-instr-use" -o ${1}.profdata.bc -pgo-test-profile-file=${1}.profdata < ${1}.ls.prof.bc > /dev/null

# We now use the profile augmented bc file as input to your pass.
opt -enable-new-pm=0 -o ${1}.fplicm.bc -load ${PATH2LIB} ${PASS} < ${1}.profdata.bc > /dev/null

# Generate binary excutable before FPLICM: Unoptimzied code
clang ${1}.ls.bc -o ${1}_no_fplicm 
# Generate binary executable after FPLICM: Optimized code
clang ${1}.fplicm.bc -o ${1}_fplicm

# Produce output from binary to check correctness
./${1}_fplicm > fplicm_output

echo -e "\n=== Correctness Check ==="
if [ "$(diff correct_output fplicm_output)" != "" ]; then
    echo -e ">> FAIL\n"
else
    echo -e ">> PASS\n"
    # Measure performance
    echo -e "1. Performance of unoptimized code"
    time ./${1}_no_fplicm > /dev/null
    echo -e "\n\n"
    echo -e "2. Performance of optimized code"
    time ./${1}_fplicm > /dev/null
    echo -e "\n\n"
fi

# Cleanup: Remove this if you want to retain the created files.
rm -f default.profraw *_prof *_fplicm *.bc *.profdata *_output *.ll
