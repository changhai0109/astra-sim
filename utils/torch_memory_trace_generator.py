import pickle, json, argparse


class TorchMemoryTraceGenerator:
    def __init__(self):
        self.trace = dict()
        self.trace["segments"] = list()
        self.trace["device_traces"] = list()
        self.trace["device_traces"].append(list())
        self.trace["allocator_settings"] = self.__class__.allocator_settings()

        self._name_map_address_size = dict()
        self._address_counter = 0

    def alloc_tensor(self, name, size, time_us):
        time_us += 1719260553557947
        device_id = 0
        address = self._address_counter
        self._address_counter += size
        if size != 0:
            # self.trace["segments"].append(self.__class__.segments_template(name, device_id, size, address))
            # self.trace["device_traces"][device_id].append(self.__class__.device_traces_seg_alloc_template(name, size, address, time_us))
            self.trace["device_traces"][device_id].append(self.__class__.device_traces_alloc_template(name, size, address, time_us+10))
        assert not name in self._name_map_address_size
        self._name_map_address_size[name] = address, size

    def dealloc_tensor(self, name, time_us):
        time_us += 1719260553557947
        device_id = 0
        address, size = self._name_map_address_size[name]
        self.trace["device_traces"][device_id].append(self.__class__.device_traces_free_request_template(name, size, address, time_us))
        self.trace["device_traces"][device_id].append(self.__class__.device_traces_free_complete_template(name, size, address, time_us+10))
        # self.trace["device_traces"][device_id].append(self.__class__.device_traces_seg_free_template(name, size, address, time_us+15))
        del self._name_map_address_size[name]

    def dump(self, filename):
        # assert len(self._name_map_address_size) == 0
        if filename.endswith("pkl"):
            with open(filename, "wb") as f:
                pickle.dump(self.trace, f)
        elif filename.endswith("json"):
            with open(filename, "w") as f:
                json.dump(self.trace, f)
        else:
            raise NotImplementedError()

    @classmethod
    def segments_template(cls, name, device_id, size, address):
        ret = {
            "device": device_id,
            "address": address,
            "total_size": size,
            "allocated_size": size,
            "active_size": size,
            "requested_size": size,
            "stream": 0,
            "segment_type": "large",
            "segment_pool_id": [0, 0],
            "is_expandable": False,
            "frames": [],
            "blocks": [
                {
                    "address": address,
                    "size": size,
                    "requested_size": size,
                    "state": "active_allocated",
                    "frames": [
                        {
                            "name": name,
                            "filename": "??",
                            "line": 0
                        }
                    ]
                }
            ]
        }
        return ret

    @classmethod
    def device_traces_seg_alloc_template(cls, name, size, address, time_us):
        ret = {
            "action": "segment_alloc",
            "addr": address,
            "size": size,
            "stream": 0,
            "time_us": time_us,
            "frames": [
                {
                    "name": name,
                    "filename": "??",
                    "line": 0
                }
            ],
        }
        return ret

    @classmethod
    def device_traces_alloc_template(cls, name, size, address, time_us):
        ret = {
            "action": "alloc",
            "addr": address,
            "size": size,
            "stream": 0,
            "time_us": time_us,
            "frames": [
                {
                    "name": name,
                    "filename": "??",
                    "line": 0
                }
            ],
        }
        return ret

    @classmethod
    def device_traces_free_request_template(cls, name, size, address, time_us):
        ret = {
            "action": "free_requested",
            "addr": address,
            "size": size,
            "stream": 0,
            "time_us": time_us,
            "frames": [
                {
                    "name": name,
                    "filename": "??",
                    "line": 0
                }
            ],
        }
        return ret

    @classmethod
    def device_traces_free_complete_template(cls, name, size, address, time_us):
        ret = {
            "action": "free_completed",
            "addr": address,
            "size": size,
            "stream": 0,
            "time_us": time_us,
            "frames": [
                {
                    "name": name,
                    "filename": "??",
                    "line": 0
                }
            ],
        }
        return ret

    @classmethod
    def device_traces_seg_free_template(cls, name, size, address, time_us):
        ret = {
            "action": "segment_free",
            "addr": address,
            "size": size,
            "stream": 0,
            "time_us": time_us,
            "frames": [
                {
                    "name": name,
                    "filename": "??",
                    "line": 0
                }
            ],
        }
        return ret


    @classmethod
    def allocator_settings(cls):
        ret = {
            "PYTORCH_CUDA_ALLOC_CONF": "",
            "max_split_size": -1,
            "garbage_collection_threshold": 0.0,
            "expandable_segments": False,
            "pinned_num_register_threads": 1,
            "release_lock_on_cudamalloc": False,
            "pinned_use_cuda_host_register": False,
            "roundup_power2_divisions": {
                "1": 0,
                "2": 0,
                "4": 0,
                "8": 0,
                "16": 0,
                "32": 0,
                "64": 0,
                "128": 0,
                "256": 0,
                "512": 0,
                "1024": 0,
                "2048": 0,
                "4096": 0,
                "8192": 0,
                "16384": 0,
                "32768": 0
            }
        }
        return ret


def transform_chrome_trace_to_torch_memory_trace(chrome_trace_path, torch_memory_trace):
    f = open(chrome_trace_path, "r")
    chrome_trace = json.load(f)
    chrome_trace = chrome_trace["traceEvents"]
    f.close()
    torch_trace = TorchMemoryTraceGenerator()
    for event in chrome_trace:
        if event["args"]["type"] != "alloc":
            continue
        name = event["name"]
        size = event["args"]["size"]
        time_us = event["ts"]
        if event["ph"] == "B":
            torch_trace.alloc_tensor(name, size, time_us)
        elif event["ph"] == "E":
            torch_trace.dealloc_tensor(name, time_us)
        else:
            assert False
    torch_trace.dump(torch_memory_trace)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input_filename",
        type=str,
        default=None,
        required=True
    )
    parser.add_argument(
        "--output_filename",
        type=str,
        default=None,
        required=True
    )
    args = parser.parse_args()

    transform_chrome_trace_to_torch_memory_trace(args.input_filename, args.output_filename)

