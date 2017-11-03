# Valid parameter types are none, perfect, static, dynamic:<# history bits>:<btb size>
# parameters=(none static perfect dynamic:0:1 dynamic:0:4 dynamic:0:8 dynamic:0:16 dynamic:0:32 dynamic:1:1 dynamic:1:4 dynamic:1:8 dynamic:1:16 dynamic:1:32 dynamic:2:1 dynamic:2:4 dynamic:2:8 dynamic:2:16 dynamic:2:32 dynamic:3:1 dynamic:3:4 dynamic:3:8 dynamic:3:16 dynamic:3:32 dynamic:4:1 dynamic:4:4 dynamic:4:8 dynamic:4:16 dynamic:4:32)
parameters=(none static perfect)

# benchmarks=(benchmarks/*.dlx)           # get list of benchmark names
benchmarks=(benchmarks/queen.dlx)       # just queen benchmark (for testing)

echo "" > results.txt                   # creates/overwrites existing results file
mkdir tmp_results                       # directory to store results in

for i in ${benchmarks[@]}; do

     # get filename (get rid of full path)
     filename=$(basename "${i}")

     for j in ${parameters[@]}; do

          # notify user of running benchmark
          printf "Running benchmark: ${filename} ${j}\n"

          # run gold standard on benchmark i with parameters j and commands in printf, storing results to tmp file
          printf "e\nt\nq\n" | golddlxBP --bp=${j} ${i} &> tmp_results/${filename}.txt

          # extract desired lines from tmp files and add to combined results
          echo "${filename} ${j} $(grep "branch:"      tmp_results/${filename}.txt)" >> combined_results.txt
          echo "${filename} ${j} $(grep "Total cycles" tmp_results/${filename}.txt)" >> combined_results.txt
     done
done

rm -rf tmp_results                      # remove temporary files
printf "Data generation complete.\n"
