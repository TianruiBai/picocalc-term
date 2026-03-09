import os
p1 = "/c/Users/weyst/Documents/picocalc-term/nuttx-apps/audioutils/Kconfig"
p2 = "C:/Users/weyst/Documents/picocalc-term/nuttx-apps/audioutils/Kconfig"
print(f"/c/ path exists: {os.path.exists(p1)}")
print(f"C:/ path exists: {os.path.exists(p2)}")
