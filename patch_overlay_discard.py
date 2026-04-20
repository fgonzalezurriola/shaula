import sys

with open("src/overlay/overlay.zig", "r") as f:
    lines = f.readlines()

new_lines = []
for line in lines:
    if line.strip() == "_ = io;":
        continue
    if line.strip() == "_ = allocator;":
        continue
    if line.strip() == "_ = err;":
        continue
    new_lines.append(line)

with open("src/overlay/overlay.zig", "w") as f:
    f.writelines(new_lines)
