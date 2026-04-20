import sys

with open("src/capture/command.zig", "r") as f:
    lines = f.readlines()

new_lines = []
in_func = False
for line in lines:
    if line.startswith("fn writeAreaDryRunJson("):
        in_func = True
    
    if in_func and line.strip().startswith("var stdout_buffer:"):
        new_lines.append('    const geometry_json = if (result.geometry) |g| blk: {\n')
        new_lines.append('        break :blk try std.fmt.allocPrint(allocator, "{{\\"x\\":{d},\\"y\\":{d},\\"width\\":{d},\\"height\\":{d}}}", .{ g.x, g.y, g.width, g.height });\n')
        new_lines.append('    } else try allocator.dupe(u8, "null");\n')
        new_lines.append('    defer allocator.free(geometry_json);\n\n')
        new_lines.append(line)
        continue

    if in_func and line.strip().startswith('"{{\\"ok\\":true,\\"contract_version\\":\\"{s}\\",\\"command\\":\\"capture area\\",\\"timestamp\\":{s},\\"selection\\":{{\\"mode\\":{s},\\"aspect\\":{s},\\"cancelled\\":false}},\\"warnings\\":[]}}\\n",'):
        new_lines.append('        "{{\\"ok\\":true,\\"contract_version\\":\\"{s}\\",\\"command\\":\\"capture area\\",\\"timestamp\\":{s},\\"selection\\":{{\\"mode\\":{s},\\"aspect\\":{s},\\"geometry\\":{s},\\"cancelled\\":false}},\\"warnings\\":[]}}\\n",\n')
        continue

    if in_func and line.strip() == ".{ protocol.contract_version, ts_json, mode_json, aspect_json },":
        new_lines.append("        .{ protocol.contract_version, ts_json, mode_json, aspect_json, geometry_json },\n")
        in_func = False
        continue

    new_lines.append(line)

with open("src/capture/command.zig", "w") as f:
    f.writelines(new_lines)
