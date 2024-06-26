#!/usr/bin/env python
import argparse
import glob
import json


def merge_traces(inputs, output):
    js = list()
    for file in inputs:
        with open(file, "r") as f:
            js.append(json.load(f))
    merged = {"traceEvents": []}
    for j in js:
        merged["traceEvents"].extend(j["traceEvents"])
    with open(output, "w") as f:
        json.dump(merged, f)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input_files", type=str, required=True)
    parser.add_argument("--merged_file", type=str, required=True)

    args = parser.parse_args()

    input_files_terms = args.input_files.split(",")
    input_files = list()
    for input_files_term in input_files_terms:
        for file in glob.glob(input_files_term):
            input_files.append(file)
    output_file = args.merged_file
    merge_traces(input_files, output_file)


if __name__ == '__main__':
    main()
