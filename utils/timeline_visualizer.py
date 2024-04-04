#!/usr/bin/env python3

## clonned from Chakra repo: https://github.com/mlcommons/chakra CommitID: dfc9d4521c4b33f0a8571b1c5cc43299ddb64b55

import argparse
import json
import logging
import sys

from typing import Any, Dict, List, Tuple
from logging import FileHandler
from enum import IntEnum

def get_logger(log_filename: str) -> logging.Logger:
    formatter = logging.Formatter(
            "%(levelname)s [%(asctime)s] %(message)s",
            datefmt="%m/%d/%Y %I:%M:%S %p")

    file_handler = FileHandler(log_filename, mode="w")
    file_handler.setLevel(logging.DEBUG)
    file_handler.setFormatter(formatter)

    stream_handler = logging.StreamHandler()
    stream_handler.setLevel(logging.WARNING)
    stream_handler.setFormatter(formatter)

    logger = logging.getLogger(__file__)
    logger.setLevel(logging.DEBUG)
    logger.addHandler(file_handler)
    logger.addHandler(stream_handler)

    return logger

def parse_event(
    line: str
) -> Tuple[str, int, int, int, str]:
    try:
        trace_type = line[line.find("[debug]")+len("[debug]"):].strip().split(",")[0]
        npu_id = int(line[line.find("sys->id=") + len("sys->id="):].split(",")[0])
        curr_cycle = int(line[line.find("tick=") + len("tick="):].split(",")[0])
        node_id = int(line[line.find("node->id=") + len("node->id="):].split(",")[0])
        node_name = line[line.find("node->name=") + len("node->name="):].split(",")[0]
        return (trace_type, npu_id, curr_cycle, node_id, node_name)
    except:
        raise ValueError(f"Cannot parse the following event -- \"{line}\"")

def get_trace_events(
    input_filename: str,
    num_npus: int,
    npu_frequency: int
) -> List[Dict[str, Any]]:
    trace_dict = {i: {} for i in range(num_npus)}
    trace_events = []

    with open(input_filename, "r") as f:
        for line in f:
            if not "[workload]" in line:
                continue
            if not "[debug]" in line:
                continue
            if ("issue" in line) or ("callback" in line):
                (trace_type, npu_id, curr_cycle, node_id, node_name) =\
                    parse_event(line)

                if trace_type == "issue":
                    tid = int(line[line.find("node_type=") + len("node_type="):])
                    trace_events.append(
                            {
                                "pid": npu_id,
                                "tid": tid,
                                "ts": curr_cycle*1e-3,
                                "ph": "B",
                                "name": node_name
                            })
                elif trace_type == "callback":
                    tid = int(line[line.find("node_type=") + len("node_type="):])
                    trace_events.append(
                            {
                                "pid": npu_id,
                                "tid": tid,
                                "ts": curr_cycle*1e-3,
                                "ph": "E",
                                "name": node_name
                            })

                else:
                    raise ValueError(f"Unsupported trace_type, {trace_type}")

    return trace_events


def write_trace_events(
    output_filename: str,
    num_npus: int,
    trace_events: List[Dict[str, Any]]
) -> None:
    output_dict = {
        "meta_user": "aras",
        "traceEvents": trace_events,
        "meta_user": "aras",
        "meta_cpu_count": num_npus
    }
    with open(output_filename, "w") as f:
        json.dump(output_dict, f)

def main() -> None:
    parser = argparse.ArgumentParser(
            description="Timeline Visualizer"
    )
    parser.add_argument(
            "--input_filename",
            type=str,
            default=None,
            required=True,
            help="Input timeline filename"
    )
    parser.add_argument(
            "--output_filename",
            type=str,
            default=None,
            required=True,
            help="Output trace filename"
    )
    parser.add_argument(
            "--num_npus",
            type=int,
            default=None,
            required=True,
            help="Number of NPUs in a system"
    )
    parser.add_argument(
            "--npu_frequency",
            type=int,
            default=None,
            required=True,
            help="NPU frequency in MHz"
    )
    parser.add_argument(
            "--log_filename",
            type=str,
            default="debug.log",
            help="Log filename"
    )
    args = parser.parse_args()

    logger = get_logger(args.log_filename)
    logger.debug(" ".join(sys.argv))

    try:
        trace_events = get_trace_events(
            args.input_filename,
            args.num_npus,
            args.npu_frequency)
        write_trace_events(
            args.output_filename,
            args.num_npus,
            trace_events)
    except Exception as e:
        logger.error(str(e))
        sys.exit(1)

if __name__ == "__main__":
    main()
