import os
import sys
import pathlib
import datetime
import subprocess



def main():
	OUTPUT_FOLDER = sys.argv[1] + "/" + str(datetime.datetime.now().isoformat()) + "/"
	pathlib.Path(OUTPUT_FOLDER).mkdir(parents=True, exist_ok=True)

	BENCHMARK_FOLDER = sys.argv[2]
	assert os.path.exists(BENCHMARK_FOLDER), f"Invalid benchmark folder: {BENCHMARK_FOLDER}"

	BINARY_PATH = sys.argv[3]
	assert os.path.isfile(BINARY_PATH), f"{BINARY_PATH} is not a file."

	NUM_OF_RUNS = int(sys.argv[4])
	assert NUM_OF_RUNS > 0, f"Cannot run {NUM_OF_RUNS} times."


	for i in NUM_OF_RUNS:
		for benchmark_name in os.listdir(BENCHMARK_FOLDER):
			with open(os.path.join(BENCHMARK_FOLDER, benchmark_name), "r") as benchmark_file:
				with open(os.path.join(OUTPUT_FOLDER, benchmark_name+"."+str(i)+".out"), "w") as output_file:
					pointer_analysis_process = subprocess.run(
						[BINARY_PATH, "-passes='print-pointer-level'", "-time-passes", "-track-memory", "--disable-output"],
						stdin=benchmark_file, stdout=output_file
						)
					output_file.flush()



if __name__ == "__main__":
	main()

	
