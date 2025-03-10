import matplotlib.pyplot as plt
import numpy as np
 
# create data
x = np.arange(10)
y1 = np.array([482.22, 0,0, 3490.79, 57.82, 0, 312.62, 6644.82, 0,0])
y2 = np.array([0,0,0,0,0,0,0,0,0,0])
y3 = np.array([ 0,0,0,0,0,0,0,0,0,0])
y4 = np.array([0,0,0,0,0,0,0,0,0,0])
width = 0.2

# plt.yscale("symlog", linthresh=1)

 
# plot bars in stack manner
plt.bar(x, y1, width, color='r')
plt.bar(x, y3, width, bottom=y1, color='y')
plt.bar(x, y4, width, bottom=y1+y3, color='g')
plt.bar(x+width, y2, width, color='b')

plt.ylim(0, 7000) 
plt.xticks(x, ["povray", "bash", "lynx", "imagick", "omnetpp", "perlbench", "xalancbmk", "parest", "gcc", "blender"])
plt.xlabel("Benchmarks")
plt.ylabel("Runtime")
plt.legend(["lfspa", "vsfs", "sfs", "levpa"])
plt.title("Runtime of algorithms on medium benchmarks")
plt.show()