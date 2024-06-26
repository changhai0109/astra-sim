/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "astra-sim/workload/Workload.hh"

#include <json/json.hpp>
#include "astra-sim/common/Logging.hh"
#include "astra-sim/system/IntData.hh"
#include "astra-sim/system/MemEventHandlerData.hh"
#include "astra-sim/system/RecvPacketEventHandlerData.hh"
#include "astra-sim/system/SendPacketEventHandlerData.hh"
#include "astra-sim/system/WorkloadLayerHandlerData.hh"
#include "astra-sim/workload/LocalMemoryTracker.hh"

#include <stdlib.h>
#include <unistd.h>
#include <iostream>

using namespace std;
using namespace AstraSim;
using namespace Chakra;
using json = nlohmann::json;

typedef ChakraProtoMsg::NodeType ChakraNodeType;
typedef ChakraProtoMsg::CollectiveCommType ChakraCollectiveCommType;

Workload::Workload(Sys* sys, string et_filename, string comm_group_filename) {
  string workload_filename = et_filename + "." + to_string(sys->id) + ".et";
  // Check if workload filename exists
  if (access(workload_filename.c_str(), R_OK) < 0) {
    string error_msg;
    if (errno == ENOENT) {
      error_msg = "workload file: " + workload_filename + " does not exist";
    } else if (errno == EACCES) {
      error_msg =
          "workload file: " + workload_filename + " exists but is not readable";
    } else {
      error_msg =
          "Unknown workload file: " + workload_filename + " access error";
    }
    LoggerFactory::get_logger("workload")->critical(error_msg);
    exit(EXIT_FAILURE);
  }
  this->et_feeder = new ETFeeder(workload_filename);
  this->comm_group = nullptr;
  // TODO: parametrize the number of available hardware resources
  this->hw_resource = new HardwareResource(1);
  this->sys = sys;
  initialize_comm_group(comm_group_filename);
  this->is_finished = false;
  if (this->sys->local_mem_tracker_enabled) {
    this->local_memory_tracker =
        new LocalMemoryTracker(this, this->sys->track_mem_activities);
  } else {
    this->local_memory_tracker = nullptr;
  }
}

Workload::~Workload() {
  if (this->comm_group != nullptr)
    delete this->comm_group;
  if (this->et_feeder != nullptr)
    delete this->et_feeder;
  if (this->hw_resource != nullptr)
    delete this->hw_resource;
  if (this->local_memory_tracker != nullptr)
    delete this->local_memory_tracker;
}

void Workload::initialize_comm_group(string comm_group_filename) {
  // communicator group input file is not given
  if (comm_group_filename.find("empty") != std::string::npos) {
    comm_group = nullptr;
    return;
  }

  ifstream inFile;
  json j;
  inFile.open(comm_group_filename);
  inFile >> j;

  for (json::iterator it = j.begin(); it != j.end(); ++it) {
    bool in_comm_group = false;

    for (auto id : it.value()) {
      if (id == sys->id) {
        in_comm_group = true;
      }
    }

    if (in_comm_group) {
      std::vector<int> involved_NPUs;
      for (auto id : it.value()) {
        involved_NPUs.push_back(id);
      }
      comm_group = new CommunicatorGroup(1, involved_NPUs, sys);
      // Note: All NPUs should create comm group with identical ids if they want
      // to communicate with each other
    }
  }
}

void Workload::issue_dep_free_nodes() {
  std::queue<shared_ptr<Chakra::ETFeederNode>> push_back_queue;
  shared_ptr<Chakra::ETFeederNode> node = et_feeder->getNextIssuableNode();
  while (node != nullptr) {
    if (hw_resource->is_available(node)) {
      issue(node);
    } else {
      push_back_queue.push(node);
    }
    node = et_feeder->getNextIssuableNode();
  }

  while (!push_back_queue.empty()) {
    shared_ptr<Chakra::ETFeederNode> node = push_back_queue.front();
    et_feeder->pushBackIssuableNode(node->id());
    push_back_queue.pop();
  }
}

void Workload::issue(shared_ptr<Chakra::ETFeederNode> node) {
  auto logger = LoggerFactory::get_logger("workload");
  hw_resource->occupy(node);
  if (sys->replay_only) {
    issue_replay(node);
  } else if (
      (node->type() == ChakraNodeType::MEM_LOAD_NODE) ||
      (node->type() == ChakraNodeType::MEM_STORE_NODE)) {
    issue_remote_mem(node);
  } else if (node->type() == ChakraNodeType::COMP_NODE) {
    if (node->is_cpu_op())
      issue_comp_cpu(node);
    else
      issue_comp_gpu(node);
  } else if (
      node->type() == ChakraNodeType::COMM_COLL_NODE ||
      node->type() == ChakraNodeType::COMM_SEND_NODE ||
      node->type() == ChakraNodeType::COMM_RECV_NODE) {
    issue_comm(node);
  } else if (node->type() == ChakraNodeType::INVALID_NODE) {
    skip_invalid(node);
  } else {
    logger->critical(
        "invalid node type. sys->id={}, node->id={} ",
        this->sys->id,
        node->id());
    exit(-1);
  }
  if (this->sys->trace_enabled) {
    logger->debug(
        "issue,sys->id={}, tick={}, node->id={}, node->name={}, node->type={}",
        sys->id,
        Sys::boostedTick(),
        node->id(),
        node->name(),
        static_cast<uint64_t>(node->type()));
  }
  if (this->local_memory_tracker != nullptr) {
    this->local_memory_tracker->issueNode(this->et_feeder, node);
  }
}

void Workload::issue_replay(shared_ptr<Chakra::ETFeederNode> node) {
  WorkloadLayerHandlerData* wlhd = new WorkloadLayerHandlerData;
  wlhd->node_id = node->id();
  uint64_t runtime = 1ul;
  if (node->runtime() != 0ul)
    // chakra runtimes are in microseconds and we should convert it into
    // nanoseconds
    runtime = node->runtime() * 1000;
  sys->register_event(this, EventType::General, wlhd, runtime);
}

void Workload::issue_remote_mem(shared_ptr<Chakra::ETFeederNode> node) {
  WorkloadLayerHandlerData* wlhd = new WorkloadLayerHandlerData;
  wlhd->sys_id = sys->id;
  wlhd->workload = this;
  wlhd->node_id = node->id();
  sys->remote_mem->issue(node->tensor_size(), wlhd);
}

void Workload::issue_comp_cpu(shared_ptr<Chakra::ETFeederNode> node) {
  if (sys->roofline_enabled) {
    WorkloadLayerHandlerData* wlhd = new WorkloadLayerHandlerData;
    wlhd->node_id = node->id();

    uint64_t io_size = node->tensor_size();
    for (const auto& parent_id : node->getChakraNode()->data_deps()) {
      auto parent = this->et_feeder->lookupNode(parent_id);
      io_size += parent->tensor_size();
    }

    double operational_intensity =
        static_cast<double>(node->num_ops()) / static_cast<double>(io_size);
    double perf = sys->roofline->get_perf(operational_intensity);
    double elapsed_time = static_cast<double>(node->num_ops()) / perf;
    uint64_t runtime = static_cast<uint64_t>(elapsed_time);
    sys->register_event(this, EventType::General, wlhd, runtime);
  } else {
    // advance this node forward the recorded "replayed" time specificed in the
    // ET.
    issue_replay(node);
  }
}

void Workload::issue_comp_gpu(shared_ptr<Chakra::ETFeederNode> node) {
  if (sys->roofline_enabled) {
    cerr
        << "roofline model for gpu is not implemented for now, change type to cpu"
        << endl;
    exit(-1);
  } else {
    // advance this node forward the recorded "replayed" time specificed in the
    // ET.
    issue_replay(node);
  }
}

void Workload::issue_comm(shared_ptr<Chakra::ETFeederNode> node) {
  vector<bool> involved_dim;
  for (size_t i = 0;
       i < this->sys->all_reduce_implementation_per_dimension.size();
       i++) {
    involved_dim.push_back(true);
  }
  // for (int i = 0; i < node->involved_dim_size(); i++) {
  // involved_dim.push_back(node->involved_dim(i));
  // }

  if (node->type() == ChakraNodeType::COMM_COLL_NODE) {
    if (node->comm_type() == ChakraCollectiveCommType::ALL_REDUCE) {
      DataSet* fp = sys->generate_all_reduce(
          node->comm_size(), involved_dim, comm_group, node->comm_priority());
      collective_comm_node_id_map[fp->my_id] = node->id();
      collective_comm_wrapper_map[fp->my_id] = fp;
      fp->set_notifier(this, EventType::CollectiveCommunicationFinished);

    } else if (node->comm_type() == ChakraCollectiveCommType::ALL_TO_ALL) {
      DataSet* fp = sys->generate_all_to_all(
          node->comm_size(), involved_dim, comm_group, node->comm_priority());
      collective_comm_node_id_map[fp->my_id] = node->id();
      collective_comm_wrapper_map[fp->my_id] = fp;
      fp->set_notifier(this, EventType::CollectiveCommunicationFinished);

    } else if (node->comm_type() == ChakraCollectiveCommType::ALL_GATHER) {
      DataSet* fp = sys->generate_all_gather(
          node->comm_size(), involved_dim, comm_group, node->comm_priority());
      collective_comm_node_id_map[fp->my_id] = node->id();
      collective_comm_wrapper_map[fp->my_id] = fp;
      fp->set_notifier(this, EventType::CollectiveCommunicationFinished);

    } else if (node->comm_type() == ChakraCollectiveCommType::REDUCE_SCATTER) {
      DataSet* fp = sys->generate_reduce_scatter(
          node->comm_size(), involved_dim, comm_group, node->comm_priority());
      collective_comm_node_id_map[fp->my_id] = node->id();
      collective_comm_wrapper_map[fp->my_id] = fp;
      fp->set_notifier(this, EventType::CollectiveCommunicationFinished);

    } else if (node->comm_type() == ChakraCollectiveCommType::BROADCAST) {
      // broadcast colelctive has not been implemented in ASTRA-SIM yet.
      // So, we just use its real system mesurements
      uint64_t runtime = 1ul;
      if (node->runtime() != 0ul)
        // chakra runtimes are in microseconds and we should convert it into
        // nanoseconds
        runtime = node->runtime() * 1000;
      DataSet* fp = new DataSet(1);
      fp->set_notifier(this, EventType::CollectiveCommunicationFinished);
      collective_comm_node_id_map[fp->my_id] = node->id();
      collective_comm_wrapper_map[fp->my_id] = fp;
      sys->register_event(
          fp,
          EventType::General,
          nullptr,
          // chakra runtimes are in microseconds and we should convert it into
          // nanoseconds
          runtime);
      fp->set_notifier(this, EventType::CollectiveCommunicationFinished);
    }
  } else if (node->type() == ChakraNodeType::COMM_SEND_NODE) {
    LoggerFactory::get_logger("workload_SR_debug")
        ->debug(
            "sys.id={}, node.id={} src={} dst={}",
            this->sys->id,
            node->id(),
            node->comm_src(),
            node->comm_dst());
    sim_request snd_req;
    snd_req.srcRank = node->comm_src();
    snd_req.dstRank = node->comm_dst();
    snd_req.reqType = UINT8;
    SendPacketEventHandlerData* sehd = new SendPacketEventHandlerData;
    sehd->callable = this;
    sehd->wlhd = new WorkloadLayerHandlerData;
    sehd->wlhd->node_id = node->id();
    sehd->event = EventType::PacketSent;
    sys->front_end_sim_send(
        0,
        Sys::dummy_data,
        node->comm_size(),
        UINT8,
        node->comm_dst(),
        node->comm_tag(),
        &snd_req,
        &Sys::handleEvent,
        sehd);
  } else if (node->type() == ChakraNodeType::COMM_RECV_NODE) {
    LoggerFactory::get_logger("workload_SR_debug")
        ->debug(
            "sys.id={}, node.id={} src={} dst={}",
            this->sys->id,
            node->id(),
            node->comm_src(),
            node->comm_dst());
    sim_request rcv_req;
    RecvPacketEventHandlerData* rcehd = new RecvPacketEventHandlerData;
    rcehd->wlhd = new WorkloadLayerHandlerData;
    rcehd->wlhd->node_id = node->id();
    rcehd->workload = this;
    rcehd->event = EventType::PacketReceived;
    sys->front_end_sim_recv(
        0,
        Sys::dummy_data,
        node->comm_size(),
        UINT8,
        node->comm_src(),
        node->comm_tag(),
        &rcv_req,
        &Sys::handleEvent,
        rcehd);
  } else {
    LoggerFactory::get_logger("workload")
        ->critical("Unknown communication node type");
    exit(EXIT_FAILURE);
  }
}

void Workload::skip_invalid(shared_ptr<Chakra::ETFeederNode> node) {
  et_feeder->freeChildrenNodes(node->id());
}

void Workload::call(EventType event, CallData* data) {
  if (is_finished) {
    return;
  }

  if (event == EventType::CollectiveCommunicationFinished) {
    IntData* int_data = (IntData*)data;
    uint64_t node_id = collective_comm_node_id_map[int_data->data];
    shared_ptr<Chakra::ETFeederNode> node = et_feeder->lookupNode(node_id);

    if (sys->trace_enabled) {
      LoggerFactory::get_logger("workload")
          ->debug(
              "callback,sys->id={}, tick={}, node->id={}, node->name={}, node->type={}",
              sys->id,
              Sys::boostedTick(),
              node->id(),
              node->name(),
              static_cast<uint64_t>(node->type()));
    }
    if (this->local_memory_tracker != nullptr) {
      this->local_memory_tracker->finishedNode(this->et_feeder, node);
    }

    hw_resource->release(node);

    et_feeder->freeChildrenNodes(node_id);

    issue_dep_free_nodes();
    if (this->local_memory_tracker == nullptr ||
        node->getChildren().size() == 0)

      // The Dataset class provides statistics that should be used later to dump
      // more statistics in the workload layer
      delete collective_comm_wrapper_map[node_id];
    collective_comm_wrapper_map.erase(node_id);

  } else {
    if (data == nullptr) {
      issue_dep_free_nodes();
    } else {
      WorkloadLayerHandlerData* wlhd = (WorkloadLayerHandlerData*)data;
      shared_ptr<Chakra::ETFeederNode> node =
          et_feeder->lookupNode(wlhd->node_id);

      if (sys->trace_enabled) {
        LoggerFactory::get_logger("workload")
            ->debug(
                "callback,sys->id={}, tick={}, node->id={}, node->name={}, node->type={}",
                sys->id,
                Sys::boostedTick(),
                node->id(),
                node->name(),
                static_cast<uint64_t>(node->type()));
      }
      if (this->local_memory_tracker != nullptr) {
        this->local_memory_tracker->finishedNode(this->et_feeder, node);
      }

      hw_resource->release(node);

      et_feeder->freeChildrenNodes(node->id());

      issue_dep_free_nodes();
      delete wlhd;
    }
  }

  if (!et_feeder->hasNodesToIssue() &&
      this->hw_resource->all_resources_released()) {
    report();
    is_finished = true;
  }
}

void Workload::fire() {
  call(EventType::General, NULL);
}

void Workload::report() {
  Tick curr_tick = Sys::boostedTick();
  LoggerFactory::get_logger("workload")
      ->info("sys[{}] finished, {} cycles", sys->id, curr_tick);
  this->local_memory_tracker->report(this->sys->memory_report_dir);
}
