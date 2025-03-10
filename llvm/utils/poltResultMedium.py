import matplotlib.pyplot as plt
import numpy as np
 
# create data
x = np.arange(16)
y1 = np.array([1.02, 1.07, 0.89, 4.63, 113.62, 10.26, 96.35, 2.26, 18.19, 1.61, 2.62, 3.478, 0.76, 0,0,0])
y2 = np.array([101.99, 156.23, 141.58, 1424.17, 0,0,0,0,0,0,0,0,0,0,0,0])
y3 = np.array([1.87, 10.92, 5.28, 10.49, 38.54, 6.96, 0,0,0,0,0,0,0,0,0,0])
y4 = np.array([1.15, 3.5, 3.06, 5.91, 14.05, 4.37, 0,0,0,0,0,0,0,0,0,0])
width = 0.2

# plt.yscale("symlog", linthresh=1)

 
# plot bars in stack manner
plt.bar(x, y1, width, color='r')
plt.bar(x, y3, width, bottom=y1, color='y')
plt.bar(x, y4, width, bottom=y1+y3, color='g')
plt.bar(x+width, y2, width, color='b')

plt.ylim(0, 20) 
plt.xticks(x, ["nab", "du", "xz", "leela", "nano", "psql", "janet", "i3", "bake", "tmux", "astyle", "x264", "ninja", "mruby", "mutt", "namd"])
plt.xlabel("Benchmarks")
plt.ylabel("Runtime")
plt.legend(["lfspa", "vsfs", "sfs", "levpa"])
plt.title("Runtime of algorithms on medium benchmarks")
plt.show()