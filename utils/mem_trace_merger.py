#!/usr/bin/env python3

import os
import json
import argparse


def main():
    # define a parser that takes two string arguments: input and output file
    parser = argparse.ArgumentParser(description='Merge multiple memory traces into one')
    parser.add_argument('--input_trace_name', type=str, help='input trace file', required=True)
    parser.add_argument('--output_trace_name', type=str, help='output merged file', required=True)
    args = parser.parse_args()
    if not "%d" in args.input_trace_name:
        print("Input trace name should be a template, like \"trace.%d.json\", where \"%d\" is the system id")
        return
    
    trace_dir, trace_name_template = os.path.split(os.path.abspath(args.input_trace_name))
    trace_filenames = os.listdir(trace_dir)

    merged_trace_events = list()
    for trace_filename in trace_filenames:
        if not trace_filename.startswith(trace_name_template.split("%d")[0]):
            continue
        if not trace_filename.endswith(trace_name_template.split("%d")[1]):
            continue
        trace = open(os.path.join(trace_dir, trace_filename), 'r')
        trace_obj = json.load(trace)
        trace.close()
        merged_trace_events.extend(trace_obj['traceEvents'])        
    
    merged_trace_file = open(args.output_trace_name, 'w')
    json.dump({'traceEvents': merged_trace_events}, merged_trace_file)
    merged_trace_file.close()


if __name__ == '__main__':
    main()
