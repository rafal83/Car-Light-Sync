#!/usr/bin/env python3
"""
Optimize sdkconfig for production (reduce firmware size)
"""
import sys

def optimize_sdkconfig(input_file, output_file):
    with open(input_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    optimized = []
    i = 0
    while i < len(lines):
        line = lines[i]

        # Optimize log level: VERBOSE -> INFO
        if line.strip() == 'CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE=y':
            optimized.append('# CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE is not set\n')
            optimized.append('CONFIG_LOG_MAXIMUM_LEVEL_INFO=y\n')
            i += 1
            continue
        elif line.strip() == 'CONFIG_LOG_MAXIMUM_LEVEL=5':
            optimized.append('CONFIG_LOG_MAXIMUM_LEVEL=3\n')
            i += 1
            continue

        # Optimize compiler: DEBUG -> SIZE
        elif line.strip() == 'CONFIG_COMPILER_OPTIMIZATION_DEBUG=y':
            optimized.append('# CONFIG_COMPILER_OPTIMIZATION_DEBUG is not set\n')
            i += 1
            continue
        elif line.strip() == '# CONFIG_COMPILER_OPTIMIZATION_SIZE is not set':
            optimized.append('CONFIG_COMPILER_OPTIMIZATION_SIZE=y\n')
            i += 1
            continue

        # Keep WiFi buffers at optimal values for performance
        # Note: Reducing buffers saves RAM but hurts performance
        # Keeping 32 buffers for responsive web interface

        # Keep all other lines as-is
        else:
            optimized.append(line)
            i += 1

    with open(output_file, 'w', encoding='utf-8') as f:
        f.writelines(optimized)

    print(f"Optimized sdkconfig created: {output_file}")
    print("  - Log level: VERBOSE -> INFO (saves ~50-100KB)")
    print("  - Compiler optimization: DEBUG -> SIZE")

if __name__ == '__main__':
    input_file = './sdkconfig.esp32c6'
    output_file = './sdkconfig.esp32c6_production'
    optimize_sdkconfig(input_file, output_file)
