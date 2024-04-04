#include "astra-sim/workload/LocalMemUsageTracker.hh"

#include <cassert>
#include <memory>
#include "astra-sim/common/Common.hh"
#include "astra-sim/common/Logging.hh"

using namespace AstraSim;
using namespace Chakra;

uint64_t LocalMemUsageTracker::parseIOInfos(
    const ChakraProtoMsg::AttributeProto& attr,
    std::vector<std::tuple<std::string, uint64_t>>& IOinfos) {
  assert(IOinfos.size() == 0ul);
  assert(attr.string_list().values().size() % 2 == 0);
  uint64_t parsed_cnt = 0ul;
  for (size_t i = 0; i < attr.string_list().values().size(); i += 2) {
    std::string name = attr.string_list().values().at(i);
    std::string sizeStr = attr.string_list().values().at(i + 1);
    uint64_t size = std::stoull(sizeStr);
    IOinfos.push_back(std::make_tuple(name, size));
    parsed_cnt++;
  }
  return parsed_cnt;
}

void LocalMemUsageTracker::recordReads(
    const std::shared_ptr<Chakra::ETFeederNode> node,
    Tick start,
    Tick end) {
  static std::vector<std::tuple<std::string, uint64_t>> IOinfos;
  IOinfos.clear();
  uint64_t nodeId = node->id();
  if (!node->has_other_attr("inputs"))
    throw MissingAttrException(
        "inputs", nodeId, "LocalMemUsageTracker::recordReads");
  auto inputsAttr = node->get_other_attr("inputs");
  this->parseIOInfos(inputsAttr, IOinfos);
  for (auto iter : IOinfos) {
    std::string tensorName = std::get<0>(iter);
    uint64_t tensorSize = std::get<1>(iter);
    MemActivity readActivity;
    readActivity.start = start;
    readActivity.end = end;
    readActivity.node = node;
    if (this->tensorSize.find(tensorName) == this->tensorSize.end()) {
      // TODO: read before write, for now assuming it exist from start.
      Logger::getLogger("workload::LocalMemUsageTracker")
          ->trace(
              "tracker record read before write node.id={} tensor.name={} start={} end={}",
              node->id(),
              tensorName,
              start,
              end);
      MemActivity writeActivity;
      writeActivity.start = 0ul;
      writeActivity.end = 10ul;
      writeActivity.node = nullptr;
      this->tensorSize.insert({tensorName, tensorSize});
      this->memWrites.insert({tensorName, writeActivity});
    }
    if (this->memReads.find(tensorName) == this->memReads.end())
      this->memReads.emplace(tensorName, std::deque<MemActivity>());
    Logger::getLogger("workload::LocalMemUsageTracker")
        ->trace(
            "tracker record read node.id={} tensor.name={} start={} end={}",
            node->id(),
            tensorName,
            start,
            end);
    this->memReads[tensorName].emplace_back(readActivity);
  }
}

void LocalMemUsageTracker::recordWrites(
    const std::shared_ptr<Chakra::ETFeederNode> node,
    Tick start,
    Tick end) {
  static std::vector<std::tuple<std::string, uint64_t>> IOinfos;
  IOinfos.clear();
  uint64_t nodeId = node->id();
  if (!node->has_other_attr("outputs"))
    throw MissingAttrException(
        "outputs", nodeId, "LocalMemUsageTracker::recordWrites");

  auto outputsAttr = node->get_other_attr("outputs");
  this->parseIOInfos(outputsAttr, IOinfos);
  for (auto iter : IOinfos) {
    std::string tensorName = std::get<0>(iter);
    uint64_t tensorSize = std::get<1>(iter);
    MemActivity writeActivity;
    writeActivity.start = start;
    writeActivity.end = end;
    writeActivity.node = node;
    Logger::getLogger("workload::LocalMemUsageTracker")
        ->trace(
            "tracker record write node.id={} tensor.name={} start={} end={}",
            node->id(),
            tensorName,
            start,
            end);
    Logger::getLogger("workload::LocalMemUsageTracker")->flush();
    if (this->tensorSize.find(tensorName) == this->tensorSize.end()) {
      // first write
      this->tensorSize.insert({tensorName, tensorSize});
      this->memWrites.insert({tensorName, writeActivity});
    } else {
      // each tensor should only be write once.
      assert(false);
      // this->memReads.insert({tensorName, writeActivity});
    }
  }
}

void LocalMemUsageTracker::recordStart(
    const std::shared_ptr<Chakra::ETFeederNode> node,
    Tick tick) {
  Logger::getLogger("workload::LocalMemUsageTracker")
      ->trace(
          "tracker record start of node.id={} at tick={}", node->id(), tick);
  this->activityStartTime.insert({node, tick});
}

void LocalMemUsageTracker::recordEnd(
    const std::shared_ptr<Chakra::ETFeederNode> node,
    Tick tick) {
  Tick start = this->activityStartTime.at(node);
  Tick end = tick;
  Logger::getLogger("workload::LocalMemUsageTracker")
      ->trace(
          "tracker record end of node.id={} at start={} end={}",
          node->id(),
          start,
          end);
  this->recordReads(node, start, end);
  this->recordWrites(node, start, end);
}

void LocalMemUsageTracker::buildMemoryTrace() {
  std::unordered_map<std::string, uint64_t> tensorMapId;
  uint64_t id_cnt = 0ul;
  for (const auto& item : tensorSize) {
    const std::string& tensorName = item.first;
    uint64_t id = id_cnt++;
    tensorMapId.insert(std::make_pair(tensorName, id));
  }
  this->serializedMemoryTrace.clear();
  for (const auto& item : tensorMapId) {
    const std::string& tensorName = item.first;
    if (this->memReads.find(tensorName) == this->memReads.end())
      continue;
    for (const auto& readActivity : this->memReads.at(tensorName)) {
      std::string node_name = "None";
      uint64_t node_id = UINT64_MAX;
      if (readActivity.node!=nullptr) {
        node_name = readActivity.node->name();
        node_id = readActivity.node->id();
      }
      json objStart = {
          {"name", tensorName},
          {"cat", "tensorRead"},
          {"ph", "B"},
          {"ts", 1e-3*readActivity.start},
          {"pid", this->sysId},
          {"tid", tensorMapId.at(tensorName)},
          {"args",
           json {
               {"size", this->tensorSize.at(tensorName)},
               {"node_name", node_name},
               {"node_id", node_id}
               }
          }
      };
      json objEnd = {
          {"name", tensorName},
          {"cat", "tensorRead"},
          {"ph", "E"},
          {"ts", 1e-3*readActivity.end},
          {"pid", this->sysId},
          {"tid", tensorMapId.at(tensorName)},
          {"args",
           json {
               {"size", this->tensorSize.at(tensorName)},
               {"node_name", node_name},
               {"node_id", node_id}
           }
          }
      };
      this->serializedMemoryTrace.push_back(std::move(objStart));
      this->serializedMemoryTrace.push_back(std::move(objEnd));
    }
  }
  for (const auto& item : tensorMapId) {
    const std::string& tensorName = item.first;
    auto writeActivity = this->memWrites.at(tensorName);
    std::string node_name = "None";
    uint64_t node_id = UINT64_MAX;
    if (writeActivity.node!=nullptr) {
      node_name = writeActivity.node->name();
      node_id = writeActivity.node->id();
    }
    json objStart = {
        {"name", tensorName},
        {"cat", "tensorWrite"},
        {"ph", "B"},
        {"ts", 1e-3*writeActivity.start},
        {"pid", this->sysId},
        {"tid", tensorMapId.at(tensorName)},
        {"args",
         json {
             {"size", this->tensorSize.at(tensorName)},
             {"node_name", node_name},
             {"node_id", node_id}
         }
        }
    };
    json objEnd = {
        {"name", tensorName},
        {"cat", "tensorWrite"},
        {"ph", "E"},
        {"ts", 1e-3*writeActivity.end},
        {"pid", this->sysId},
        {"tid", tensorMapId.at(tensorName)},
        {"args",
         json {
             {"size", this->tensorSize.at(tensorName)},
             {"node_name", node_name},
             {"node_id", node_id}
         }
        }
    };
    this->serializedMemoryTrace.push_back(std::move(objStart));
    this->serializedMemoryTrace.push_back(std::move(objEnd));
  }
}

void LocalMemUsageTracker::dumpMemoryTrace(const std::string& filename) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    Logger::getLogger("workload::LocalMemUsageTracker")
        ->error("failed to open file {}", filename);
    return;
  }
  json trace;
  trace["traceEvents"] = this->serializedMemoryTrace;
  file << trace.dump(2);
  file.close();
}
