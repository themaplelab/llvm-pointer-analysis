import os
import sys
import pathlib
import datetime
import subprocess
import tqdm



def main():
  OUTPUT_FOLDER = sys.argv[1] + "/" + str(datetime.datetime.now().isoformat()) + "/"
  pathlib.Path(OUTPUT_FOLDER).mkdir(parents=True, exist_ok=True)

  BENCHMARK_FOLDER = sys.argv[2]
  assert os.path.exists(BENCHMARK_FOLDER), f"Invalid benchmark folder: {BENCHMARK_FOLDER}"

  LLVM_BINARY_PATH = sys.argv[3]
  assert os.path.isfile(LLVM_BINARY_PATH), f"{LLVM_BINARY_PATH} is not a file."

  NUM_OF_RUNS = int(sys.argv[4])
  assert NUM_OF_RUNS > 0, f"Cannot run {NUM_OF_RUNS} times."

  SVF_PATH = sys.argv[5]
  assert os.path.isfile(SVF_PATH), f"{SVF_PATH} is not a file."

  if sys.platform == "darwin":
    TIME_OPTION = "-l"
  elif sys.platform == "linux":
    TIME_OPTION = "-v"





  for i in tqdm.tqdm(range(NUM_OF_RUNS)):
    for benchmark_name in os.listdir(BENCHMARK_FOLDER):
      with open(os.path.join(BENCHMARK_FOLDER, benchmark_name), "r") as benchmark_file:
        with open(os.path.join(OUTPUT_FOLDER, benchmark_name+".lfspa."+str(i)+".out"), "w") as output_file:
          subprocess.run(
            [LLVM_BINARY_PATH, "-passes=lfspa-print", "-time-passes", "-track-memory", "--disable-output"],
            stdin=benchmark_file, stderr=output_file, stdout=output_file
            )
          output_file.flush()

      with open(os.path.join(BENCHMARK_FOLDER, benchmark_name), "r") as benchmark_file:
        with open(os.path.join(OUTPUT_FOLDER, benchmark_name+".levpa."+str(i)+".out"), "w") as output_file:
          subprocess.run(
            [LLVM_BINARY_PATH, "-passes=levpa-print", "-time-passes", "-track-memory", "--disable-output"],
            stdin=benchmark_file, stderr=output_file, stdout=output_file
            )
          output_file.flush()

      with open(os.path.join(OUTPUT_FOLDER, benchmark_name+".sfs."+str(i)+".out"), "w") as output_file:
        subprocess.run(
          ["/usr/bin/time", TIME_OPTION, SVF_PATH, "-fspta", "-marked-clocks-only", os.path.join(BENCHMARK_FOLDER, benchmark_name)],
          stderr=output_file, stdout=output_file
          )
        output_file.flush()


      with open(os.path.join(OUTPUT_FOLDER, benchmark_name+".vsfs."+str(i)+".out"), "w") as output_file:
        subprocess.run(
          ["/usr/bin/time", TIME_OPTION, SVF_PATH, "-vfspta", "-marked-clocks-only", os.path.join(BENCHMARK_FOLDER, benchmark_name)],
          stderr=output_file, stdout=output_file
          )
        output_file.flush()
          



if __name__ == "__main__":
  main()

  
