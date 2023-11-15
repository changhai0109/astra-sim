/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "astra-sim/workload/Workload.hh"

#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <memory>
#include <sstream>

#include "astra-sim/system/IntData.hh"
#include "astra-sim/system/MemEventHandlerData.hh"
#include "astra-sim/system/RecvPacketEventHandlerData.hh"
#include "astra-sim/system/SendPacketEventHandlerData.hh"
#include "astra-sim/system/WorkloadLayerHandlerData.hh"
#include "astra-sim/utils/Logging.hh"
#include "json/json.hpp"

using namespace AstraSim;
using namespace Chakra;
using json = nlohmann::json;

typedef ChakraProtoMsg::NodeType ChakraNodeType;
typedef ChakraProtoMsg::MemoryType ChakraMemoryType;
typedef ChakraProtoMsg::CollectiveCommType ChakraCollectiveCommType;

Workload::Workload(
    Sys* sys,
    std::string eg_filename,
    std::string comm_group_filename) {
  std::string workload_filename =
      eg_filename + "." + std::to_string(sys->id) + ".eg";
  // Check if workload filename exists
  if (access(workload_filename.c_str(), R_OK) < 0) {
    std::string error_msg;
    if (errno == ENOENT) {
      error_msg = "workload file: " + workload_filename + " does not exist";
    } else if (errno == EACCES) {
      error_msg =
          "workload file: " + workload_filename + " exists but is not readable";
    } else {
      error_msg =
          "Unknown workload file: " + workload_filename + " access error";
    }
    Logger::getLogger("workload")->critical(error_msg);
    exit(EXIT_FAILURE);
  }
  this->et_feeder = new ETFeeder(workload_filename);
  this->comm_group = nullptr;
  // TODO: parametrize the number of available hardware resources
  this->hw_resource = new HardwareResource(1);
  this->sys = sys;
  initialize_comm_group(comm_group_filename);
  this->is_finished = false;
}

Workload::~Workload() {
  if (this->comm_group != nullptr)
    delete this->comm_group;
  if (this->et_feeder != nullptr)
    delete this->et_feeder;
}

void Workload::initialize_comm_group(std::string comm_group_filename) {
  // communicator group input file is not given
  if (comm_group_filename.find("empty") != std::string::npos) {
    comm_group = nullptr;
    return;
  }

  std::ifstream inFile;
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
  std::queue<std::shared_ptr<Chakra::ETFeederNode>> push_back_queue;
  std::shared_ptr<Chakra::ETFeederNode> node = et_feeder->getNextIssuableNode();
  while (node != nullptr) {
    if (hw_resource->is_available(node)) {
      issue(node);
    } else {
      push_back_queue.push(node);
    }
    node = et_feeder->getNextIssuableNode();
  }

  while (!push_back_queue.empty()) {
    std::shared_ptr<Chakra::ETFeederNode> node = push_back_queue.front();
    et_feeder->pushBackIssuableNode(node->getChakraNode()->id());
    push_back_queue.pop();
  }
}

void Workload::node_sanity_check(std::shared_ptr<Chakra::ETFeederNode> node) {
  const auto& chakra_node = node->getChakraNode();
  if ((chakra_node->node_type() == ChakraNodeType::MEM_LOAD_NODE) ||
      (chakra_node->node_type() == ChakraNodeType::MEM_STORE_NODE)) {
    if (!(chakra_node->tensor_loc() == ChakraMemoryType::REMOTE_MEMORY))
      throw std::invalid_argument("invalid tensor location for memory node");
    if (!(chakra_node->has_tensor_size()))
      throw std::invalid_argument("empty tensor size for memory node");
    return;
  } else if (chakra_node->node_type() == ChakraNodeType::COMP_NODE) {
    if (sys->roofline_enabled) {
      if (!(chakra_node->has_num_ops()))
        throw std::invalid_argument(
            "Empty num_ops! With roofline model, comp node should have num_ops");
      if (!(chakra_node->has_tensor_size()))
        throw std::invalid_argument(
            "Empty tensor_size! With roofline model, comp node should have tensor_size");
    } else {
      if (!(chakra_node->has_simulated_run_time()))
        throw std::invalid_argument(
            "Empty simulated_run_time! Without roofline model, comp node should have simulated_run_time");
    }
    return;
  } else if (
      (chakra_node->node_type() == ChakraNodeType::COMM_COLL_NODE) ||
      (chakra_node->node_type() == ChakraNodeType::COMM_SEND_NODE) ||
      (chakra_node->node_type() == ChakraNodeType::COMM_RECV_NODE)) {
    if (!(chakra_node->has_comm_size()))
      throw std::invalid_argument("Empty comm_size for comm node!");
    if (chakra_node->node_type() == ChakraNodeType::COMM_COLL_NODE) {
      switch (chakra_node->comm_type()) {
        // for all implemented comm types, break: please make sure list here
        // align to issue_comm implementation
        case ChakraCollectiveCommType::ALL_REDUCE:
        case ChakraCollectiveCommType::ALL_GATHER:
        case ChakraCollectiveCommType::ALL_TO_ALL:
        case ChakraCollectiveCommType::REDUCE_SCATTER:
          break;
        // for other comm types, shouldnt be here, trigger exception
        default:
          throw std::invalid_argument("Invalid comm type for COMM_COLL_NODE");
      }
    }
    return;
  }

  throw std::invalid_argument("Invalid node type");
}

void Workload::issue(std::shared_ptr<Chakra::ETFeederNode> node) {
  // In strict_mode, the node field check will be enforced, and the simulation
  // will stop if field check fails. Otherwise, just skip these invalid nodes
  // and continue run (default behavior)
  constexpr bool strict_mode = true;
  std::shared_ptr<spdlog::logger> logger = Logger::getLogger("workload");
  std::stringstream sstream_buffer;
  try {
    node_sanity_check(node);
  } catch (std::invalid_argument& e) {
    // conditional catch when not strict_mode
    if (!strict_mode) {
      logger->warn(e.what());
      skip_invalid(node);
      return;
    } else {
      throw e;
    }
  }

  // from here the node should be valid and following code should run well.
  if (sys->trace_enabled) {
    sstream_buffer.str("");
    sstream_buffer << "issue,sys->id=" << sys->id
                   << ",tick=" << Sys::boostedTick()
                   << ",node->id=" << node->getChakraNode()->id()
                   << ",node->name=" << node->getChakraNode()->name()
                   << ",node->node_type=" << node->getChakraNode()->node_type();
    logger->debug(sstream_buffer.str());
  }

  if ((node->getChakraNode()->node_type() == ChakraNodeType::MEM_LOAD_NODE) ||
      (node->getChakraNode()->node_type() == ChakraNodeType::MEM_STORE_NODE)) {
    issue_remote_mem(node);
  } else if (node->getChakraNode()->node_type() == ChakraNodeType::COMP_NODE) {
    issue_comp(node);
  } else if (
      (node->getChakraNode()->node_type() == ChakraNodeType::COMM_COLL_NODE) ||
      (node->getChakraNode()->node_type() == ChakraNodeType::COMM_SEND_NODE) ||
      (node->getChakraNode()->node_type() == ChakraNodeType::COMM_RECV_NODE)) {
    issue_comm(node);
  }
}

void Workload::issue_remote_mem(std::shared_ptr<Chakra::ETFeederNode> node) {
  hw_resource->occupy(node);

  WorkloadLayerHandlerData* wlhd = new WorkloadLayerHandlerData;
  wlhd->sys_id = sys->id;
  wlhd->workload = this;
  wlhd->node_id = node->getChakraNode()->id();
  sys->remote_mem->issue(node->getChakraNode()->tensor_size(), wlhd);
}

void Workload::issue_comp(std::shared_ptr<Chakra::ETFeederNode> node) {
  hw_resource->occupy(node);

  if (sys->roofline_enabled) {
    WorkloadLayerHandlerData* wlhd = new WorkloadLayerHandlerData;
    wlhd->node_id = node->getChakraNode()->id();

    double operational_intensity =
        static_cast<double>(node->getChakraNode()->num_ops()) /
        static_cast<double>(node->getChakraNode()->tensor_size());
    double perf = sys->roofline->get_perf(operational_intensity);
    double elapsed_time =
        static_cast<double>(node->getChakraNode()->num_ops()) / perf;
    uint64_t simulated_run_time =
        static_cast<uint64_t>(elapsed_time * static_cast<double>(FREQ));
    sys->register_event(this, EventType::General, wlhd, simulated_run_time);
  } else {
    WorkloadLayerHandlerData* wlhd = new WorkloadLayerHandlerData;
    wlhd->node_id = node->getChakraNode()->id();

    sys->register_event(
        this,
        EventType::General,
        wlhd,
        node->getChakraNode()->simulated_run_time());
  }
}

void Workload::issue_comm(std::shared_ptr<Chakra::ETFeederNode> node) {
  hw_resource->occupy(node);

  std::vector<bool> involved_dim;
  for (int i = 0; i < node->getChakraNode()->involved_dim_size(); i++) {
    involved_dim.push_back(node->getChakraNode()->involved_dim(i));
  }

  if (node->getChakraNode()->node_type() == ChakraNodeType::COMM_COLL_NODE) {
    if (node->getChakraNode()->comm_type() ==
        ChakraCollectiveCommType::ALL_REDUCE) {
      DataSet* fp = sys->generate_all_reduce(
          node->getChakraNode()->comm_size(),
          involved_dim,
          comm_group,
          node->getChakraNode()->comm_priority());
      collective_comm_node_id_map[fp->my_id] = node->getChakraNode()->id();
      fp->set_notifier(this, EventType::CollectiveCommunicationFinished);

    } else if (
        node->getChakraNode()->comm_type() ==
        ChakraCollectiveCommType::ALL_TO_ALL) {
      DataSet* fp = sys->generate_all_to_all(
          node->getChakraNode()->comm_size(),
          involved_dim,
          comm_group,
          node->getChakraNode()->comm_priority());
      collective_comm_node_id_map[fp->my_id] = node->getChakraNode()->id();
      fp->set_notifier(this, EventType::CollectiveCommunicationFinished);

    } else if (
        node->getChakraNode()->comm_type() ==
        ChakraCollectiveCommType::ALL_GATHER) {
      DataSet* fp = sys->generate_all_gather(
          node->getChakraNode()->comm_size(),
          involved_dim,
          comm_group,
          node->getChakraNode()->comm_priority());
      collective_comm_node_id_map[fp->my_id] = node->getChakraNode()->id();
      fp->set_notifier(this, EventType::CollectiveCommunicationFinished);

    } else if (
        node->getChakraNode()->comm_type() ==
        ChakraCollectiveCommType::REDUCE_SCATTER) {
      DataSet* fp = sys->generate_reduce_scatter(
          node->getChakraNode()->comm_size(),
          involved_dim,
          comm_group,
          node->getChakraNode()->comm_priority());
      collective_comm_node_id_map[fp->my_id] = node->getChakraNode()->id();
      fp->set_notifier(this, EventType::CollectiveCommunicationFinished);
    }
  } else if (
      node->getChakraNode()->node_type() == ChakraNodeType::COMM_SEND_NODE) {
    sim_request snd_req;
    snd_req.srcRank = node->getChakraNode()->comm_src();
    snd_req.dstRank = node->getChakraNode()->comm_dst();
    snd_req.reqType = UINT8;
    SendPacketEventHandlerData* sehd = new SendPacketEventHandlerData;
    sehd->callable = this;
    sehd->wlhd = new WorkloadLayerHandlerData;
    sehd->wlhd->node_id = node->getChakraNode()->id();
    sehd->event = EventType::PacketSent;
    sys->front_end_sim_send(
        0,
        Sys::dummy_data,
        node->getChakraNode()->comm_size(),
        UINT8,
        node->getChakraNode()->comm_dst(),
        node->getChakraNode()->comm_tag(),
        &snd_req,
        &Sys::handleEvent,
        sehd);
  } else if (
      node->getChakraNode()->node_type() == ChakraNodeType::COMM_RECV_NODE) {
    sim_request rcv_req;
    RecvPacketEventHandlerData* rcehd = new RecvPacketEventHandlerData;
    rcehd->wlhd = new WorkloadLayerHandlerData;
    rcehd->wlhd->node_id = node->getChakraNode()->id();
    rcehd->workload = this;
    rcehd->event = EventType::PacketReceived;
    sys->front_end_sim_recv(
        0,
        Sys::dummy_data,
        node->getChakraNode()->comm_size(),
        UINT8,
        node->getChakraNode()->comm_src(),
        node->getChakraNode()->comm_tag(),
        &rcv_req,
        &Sys::handleEvent,
        rcehd);
  } else {
    Logger::getLogger("workload")->critical("Unknown communication node type");
    exit(EXIT_FAILURE);
  }
}

void Workload::skip_invalid(std::shared_ptr<Chakra::ETFeederNode> node) {
  et_feeder->freeChildrenNodes(node->getChakraNode()->id());
  et_feeder->removeNode(node->getChakraNode()->id());
}

void Workload::call(EventType event, CallData* data) {
  if (is_finished) {
    return;
  }
  std::shared_ptr<spdlog::logger> logger = Logger::getLogger("workload");
  std::stringstream sstream_buffer;

  if (event == EventType::CollectiveCommunicationFinished) {
    IntData* int_data = (IntData*)data;
    uint64_t node_id = collective_comm_node_id_map[int_data->data];
    std::shared_ptr<Chakra::ETFeederNode> node = et_feeder->lookupNode(node_id);

    if (sys->trace_enabled) {
      sstream_buffer.str("");
      sstream_buffer << "callback,sys->id=" << sys->id
                     << ",tick=" << Sys::boostedTick()
                     << ",node->id=" << node->getChakraNode()->id()
                     << ",node->name=" << node->getChakraNode()->name()
                     << ",node->node_type="
                     << node->getChakraNode()->node_type();
      logger->debug(sstream_buffer.str());
    }

    hw_resource->release(node);

    et_feeder->freeChildrenNodes(node_id);

    issue_dep_free_nodes();

    et_feeder->removeNode(node_id);

  } else {
    if (data == nullptr) {
      issue_dep_free_nodes();
    } else {
      WorkloadLayerHandlerData* wlhd = (WorkloadLayerHandlerData*)data;
      std::shared_ptr<Chakra::ETFeederNode> node =
          et_feeder->lookupNode(wlhd->node_id);

      if (sys->trace_enabled) {
        sstream_buffer.str("");
        sstream_buffer << "callback,sys->id=" << sys->id
                       << ",tick=" << Sys::boostedTick()
                       << ",node->id=" << node->getChakraNode()->id()
                       << ",node->name=" << node->getChakraNode()->name()
                       << ",node->node_type="
                       << node->getChakraNode()->node_type();
        logger->debug(sstream_buffer.str());
      }

      hw_resource->release(node);

      et_feeder->freeChildrenNodes(node->getChakraNode()->id());

      issue_dep_free_nodes();

      et_feeder->removeNode(wlhd->node_id);
      delete wlhd;
    }
  }

  if (!et_feeder->hasNodesToIssue() &&
      (hw_resource->num_in_flight_mem_reqs == 0) &&
      (hw_resource->num_in_flight_comps == 0) &&
      (hw_resource->num_in_flight_comms == 0)) {
    report();
    is_finished = true;
  }
}

void Workload::fire() {
  call(EventType::General, NULL);
}

void Workload::report() {
  Tick curr_tick = Sys::boostedTick();
  std::shared_ptr<spdlog::logger> logger = Logger::getLogger("workload");
  logger->info("sys[{}] finished, {} cycles", sys->id, curr_tick);
}
