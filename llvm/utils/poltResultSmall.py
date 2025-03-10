import matplotlib.pyplot as plt
import numpy as np
 
# create data
x = np.arange(4)
y1 = np.array([0.05, 0.09, 0.43, 1.39])
y2 = np.array([0.43, 1.12, 9.95, 302.69])
y3 = np.array([0.07, 0.19, 0.37, 20.03])
y4 = np.array([0.04, 0.12, 0.21, 8.13])
width = 0.2

# plt.yscale("symlog", linthresh=1)

 
# plot bars in stack manner
plt.bar(x, y1, width, color='r')
plt.bar(x, y3, width, bottom=y1, color='y')
plt.bar(x, y4, width, bottom=y1+y3, color='g')
plt.bar(x+width, y2, width, color='b')

plt.ylim(0, 10) 
plt.xticks(x, ['lbm', 'mcf', 'deepsjeng', 'dpkg'])
plt.xlabel("Benchmarks")
plt.ylabel("Runtime")
plt.legend(["lfspa", "vsfs", "sfs", "levpa"])
plt.title("Runtime of algorithms on small benchmarks")
plt.show()