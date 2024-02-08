/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

// TODO: HardwareResource.cc should be moved to the system layer.

#include "astra-sim/workload/HardwareResource.hh"
#include "astra-sim/common/Logging.hh"

using namespace std;
using namespace AstraSim;
using namespace Chakra;

typedef ChakraProtoMsg::NodeType ChakraNodeType;

HardwareResource::HardwareResource(uint32_t num_npus)
    : num_npus(num_npus),
      num_in_flight_cpu_comp_ops(0u),
      num_in_flight_gpu_comp_ops(0u),
      num_in_flight_comm_ops(0u),
      num_in_flight_mem_ops(0u) {}

void HardwareResource::occupy(const shared_ptr<Chakra::ETFeederNode> node) {
  auto logger = Logger::getLogger("workload::HardwareResource");
  switch (node->type()) {
    case ChakraNodeType::COMP_NODE:
      if (node->is_cpu_op(true)) {
        assert(num_in_flight_cpu_comp_ops == 0);
        ++num_in_flight_cpu_comp_ops;
#ifdef ASTRASIM_TRACE_HARDWARE_RESOURCES
        logger->trace("Node.id={} occupy cpu_comp_ops", node->id());
#endif
      } else {
        assert(num_in_flight_gpu_comp_ops == 0);
        ++num_in_flight_gpu_comp_ops;
#ifdef ASTRASIM_TRACE_HARDWARE_RESOURCES
        logger->trace("Node.id={} occupy gpu_comp_ops", node->id());
#endif
      }
      break;
    case ChakraNodeType::COMM_COLL_NODE:
    case ChakraNodeType::COMM_SEND_NODE:
      assert(num_in_flight_comm_ops == 0);
      ++num_in_flight_comm_ops;
#ifdef ASTRASIM_TRACE_HARDWARE_RESOURCES
      logger->trace("Node.id={} occupy comm_ops", node->id());
#endif
      break;
    case ChakraNodeType::MEM_LOAD_NODE:
    case ChakraNodeType::MEM_STORE_NODE:
      assert(num_in_flight_mem_ops == 0);
      ++num_in_flight_mem_ops;
#ifdef ASTRASIM_TRACE_HARDWARE_RESOURCES
      logger->trace("Node.id={} occupy mem_ops", node->id());
#endif
      break;
    case ChakraNodeType::COMM_RECV_NODE: // the recv should be non-blocking and
                                         // do not occupy resources
    case ChakraNodeType::INVALID_NODE:
      break;
    default:
      assert(false);
  }
}

void HardwareResource::release(const shared_ptr<Chakra::ETFeederNode> node) {
  auto logger = Logger::getLogger("workload::HardwareResource");
  switch (node->type()) {
    case ChakraNodeType::COMP_NODE:
      if (node->is_cpu_op(true)) {
        assert(num_in_flight_cpu_comp_ops > 0);
        --num_in_flight_cpu_comp_ops;
#ifdef ASTRASIM_TRACE_HARDWARE_RESOURCES
        logger->trace("Node.id={} release cpu_comp_ops", node->id());
#endif
      } else {
        assert(num_in_flight_gpu_comp_ops > 0);
        --num_in_flight_gpu_comp_ops;
#ifdef ASTRASIM_TRACE_HARDWARE_RESOURCES
        logger->trace("Node.id={} release gpu_comp_ops", node->id());
#endif
      }
      break;
    case ChakraNodeType::COMM_COLL_NODE:
    case ChakraNodeType::COMM_SEND_NODE:
      assert(num_in_flight_comm_ops > 0);
      --num_in_flight_comm_ops;
#ifdef ASTRASIM_TRACE_HARDWARE_RESOURCES
      logger->trace("Node.id={} release comm_ops", node->id());
#endif
      break;
    case ChakraNodeType::MEM_LOAD_NODE:
    case ChakraNodeType::MEM_STORE_NODE:
      assert(num_in_flight_mem_ops > 0);
      --num_in_flight_mem_ops;
#ifdef ASTRASIM_TRACE_HARDWARE_RESOURCES
      logger->trace("Node.id={} release mem_ops", node->id());
#endif
      break;
    case ChakraNodeType::COMM_RECV_NODE: // the recv should be non-blocking and
                                         // do not occupy resources
    case ChakraNodeType::INVALID_NODE:
      break;
    default:
      assert(false);
  }
}

bool HardwareResource::is_available(
    const shared_ptr<Chakra::ETFeederNode> node) const {
  auto logger = Logger::getLogger("workload::HardwareResource");
#ifdef ASTRASIM_TRACE_HARDWARE_RESOURCES
  logger->trace(
      "Current hw resources: cpu_comp_ops={}, gpu_comp_ops={}, comm_ops={}, comm_ops={}",
      num_in_flight_cpu_comp_ops,
      num_in_flight_gpu_comp_ops,
      num_in_flight_comm_ops,
      num_in_flight_mem_ops);
#endif
  switch (node->type()) {
    case ChakraNodeType::COMP_NODE:
      if (node->is_cpu_op(true))
        return num_in_flight_cpu_comp_ops == 0;
      else
        return num_in_flight_gpu_comp_ops == 0;

    case ChakraNodeType::COMM_COLL_NODE:
    case ChakraNodeType::COMM_SEND_NODE:
    case ChakraNodeType::COMM_RECV_NODE:
      return num_in_flight_comm_ops == 0;

    case ChakraNodeType::MEM_LOAD_NODE:
    case ChakraNodeType::MEM_STORE_NODE:
      return num_in_flight_mem_ops == 0;

    case ChakraNodeType::INVALID_NODE:
      return true;

    default:
      assert(false);
  }
}
