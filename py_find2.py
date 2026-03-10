import os
for root, dirs, files in os.walk("."):
    if ".git" in root or "obj" in root: continue
    for file in files:
        if True:
            try:
                if "Bringing up app" in open(os.path.join(root, file), errors="ignore").read():
                    print(os.path.join(root, file))
            except Exception as e:
                pass
