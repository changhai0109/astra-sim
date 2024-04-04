#include "astra-sim/workload/LocalMemUsageTracker.hh"

#include <cassert>
#include <memory>
#include <unordered_set>
#include "astra-sim/common/Common.hh"
#include "astra-sim/common/Logging.hh"

using namespace AstraSim;
using namespace Chakra;

uint64_t LocalMemUsageTracker::parseIOInfos(
    const ChakraProtoMsg::AttributeProto& attr,
    std::vector<std::tuple<TensorId, uint64_t>>& IOinfos) {
  assert(IOinfos.size() == 0ul);
  assert(attr.string_list().values().size() % 2 == 0);
  uint64_t parsedCnt = 0ul;
  for (int32_t i = 0; i < attr.string_list().values().size(); i += 2) {
    TensorId name = attr.string_list().values().at(i);
    std::string sizeStr = attr.string_list().values().at(i + 1);
    uint64_t size = std::stoull(sizeStr);
    IOinfos.push_back(std::make_tuple(name, size));
    parsedCnt++;
  }
  return parsedCnt;
}

void LocalMemUsageTracker::recordReads(
    const std::shared_ptr<Chakra::ETFeederNode> node,
    Tick start,
    Tick end) {
  static std::vector<std::tuple<TensorId, uint64_t>> IOinfos;
  IOinfos.clear();
  uint64_t nodeId = node->id();
  if (!node->has_other_attr("inputs"))
    throw MissingAttrException(
        "inputs", nodeId, "LocalMemUsageTracker::recordReads");
  auto inputsAttr = node->get_other_attr("inputs");
  this->parseIOInfos(inputsAttr, IOinfos);
  for (auto iter : IOinfos) {
    TensorId tensorName = std::get<0>(iter);
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
      this->memReads.emplace(tensorName, std::vector<MemActivity>());
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
  static std::vector<std::tuple<TensorId, uint64_t>> IOinfos;
  IOinfos.clear();
  uint64_t nodeId = node->id();
  if (!node->has_other_attr("outputs"))
    throw MissingAttrException(
        "outputs", nodeId, "LocalMemUsageTracker::recordWrites");

  auto outputsAttr = node->get_other_attr("outputs");
  this->parseIOInfos(outputsAttr, IOinfos);
  for (auto iter : IOinfos) {
    TensorId tensorName = std::get<0>(iter);
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
  this->tensorMapId.clear();
  uint64_t idCnt = 0ul;
  for (const auto& item : tensorSize) {
    const TensorId& tensorName = item.first;
    uint64_t id = idCnt++;
    tensorMapId.insert(std::make_pair(tensorName, id));
  }
  this->serializedMemoryTrace.clear();
  for (const auto& item : tensorMapId) {
    const TensorId& tensorName = item.first;
    if (this->memReads.find(tensorName) == this->memReads.end())
      continue;
    for (const auto& readActivity : this->memReads.at(tensorName)) {
      std::string nodeName = "UNDEFINED";
      uint64_t nodeId = UINT64_MAX;
      if (readActivity.node != nullptr) {
        nodeName = readActivity.node->name();
        nodeId = readActivity.node->id();
      }
      json objStart = {
          {"name", tensorName},
          {"cat", "tensorRead"},
          {"ph", "B"},
          {"ts", 1e-3 * readActivity.start},
          {"pid", this->sysId},
          {"tid", tensorMapId.at(tensorName)},
          {"args",
           json{
               {"size", this->tensorSize.at(tensorName)},
               {"node_name", nodeName},
               {"node_id", nodeId}}}};
      json objEnd = {
          {"name", tensorName},
          {"cat", "tensorRead"},
          {"ph", "E"},
          {"ts", 1e-3 * readActivity.end},
          {"pid", this->sysId},
          {"tid", tensorMapId.at(tensorName)},
          {"args",
           json{
               {"size", this->tensorSize.at(tensorName)},
               {"node_name", nodeName},
               {"node_id", nodeId}}}};
      this->serializedMemoryTrace.push_back(std::move(objStart));
      this->serializedMemoryTrace.push_back(std::move(objEnd));
    }
  }
  for (const auto& item : tensorMapId) {
    const TensorId& tensorName = item.first;
    auto writeActivity = this->memWrites.at(tensorName);
    std::string nodeName = "UNDEFINED";
    uint64_t nodeId = UINT64_MAX;
    if (writeActivity.node != nullptr) {
      nodeName = writeActivity.node->name();
      nodeId = writeActivity.node->id();
    }
    json objStart = {
        {"name", tensorName},
        {"cat", "tensorWrite"},
        {"ph", "B"},
        {"ts", 1e-3 * writeActivity.start},
        {"pid", this->sysId},
        {"tid", tensorMapId.at(tensorName)},
        {"args",
         json{
             {"size", this->tensorSize.at(tensorName)},
             {"node_name", nodeName},
             {"node_id", nodeId}}}};
    json objEnd = {
        {"name", tensorName},
        {"cat", "tensorWrite"},
        {"ph", "E"},
        {"ts", 1e-3 * writeActivity.end},
        {"pid", this->sysId},
        {"tid", tensorMapId.at(tensorName)},
        {"args",
         json{
             {"size", this->tensorSize.at(tensorName)},
             {"node_name", nodeName},
             {"node_id", nodeId}}}};
    this->serializedMemoryTrace.push_back(std::move(objStart));
    this->serializedMemoryTrace.push_back(std::move(objEnd));
  }
}

void LocalMemUsageTracker::dumpMemoryTrace(const std::string& filename) {
  std::string local_mem_trace_filename = fmt::format(filename+".{}.json", this->sysId);
  std::ofstream file(local_mem_trace_filename);
  if (!file.is_open()) {
    Logger::getLogger("workload::LocalMemUsageTracker")
        ->error("failed to open file {}", local_mem_trace_filename);
    return;
  }
  json trace;
  trace["traceEvents"] = this->serializedMemoryTrace;
  file << trace.dump(2);
  file.close();
}

void LocalMemUsageTracker::buildMemoryTimeline() {
  std::set<Tick> ticks;
  std::map<Tick, std::vector<TensorId>> tensorWrites;
  std::map<Tick, std::vector<TensorId>> tensorLastReads;
  for (const auto& item : this->memWrites) {
    const TensorId& tensorName = item.first;
    const MemActivity& writeActivity = item.second;
    ticks.insert(writeActivity.start);
    if (tensorWrites.find(writeActivity.start) == tensorWrites.end())
      tensorWrites[writeActivity.start] = std::vector<TensorId>();
    tensorWrites[writeActivity.start].push_back(tensorName);
    json tensorWriteEvent = {
        {"name", tensorName},
        {"cat", "tensorLifetime"},
        {"ph", "B"},
        {"ts", 1e-3 * writeActivity.start},
        {"pid", this->sysId + 1000000ul},
        {"tid", this->tensorMapId.at(tensorName)},
        {"args", json{{"size", this->tensorSize.at(tensorName)}}}};
    this->serializedMemoryTrace.push_back(std::move(tensorWriteEvent));
  }
  for (const auto& item : this->memReads) {
    const TensorId& tensorName = item.first;
    const MemActivity& latestReadActivity = item.second.back();
    ticks.insert(latestReadActivity.end);
    if (tensorLastReads.find(latestReadActivity.end) == tensorLastReads.end())
      tensorLastReads[latestReadActivity.end] = std::vector<TensorId>();
    tensorLastReads[latestReadActivity.end].push_back(tensorName);
    json tensorLastReadEvent = {
        {"name", tensorName},
        {"cat", "tensorLifetime"},
        {"ph", "E"},
        {"ts", 1e-3 * latestReadActivity.end},
        {"pid", this->sysId + 1000000ul},
        {"tid", this->tensorMapId.at(tensorName)},
        {"args", json{{"size", this->tensorSize.at(tensorName)}}}};
    this->serializedMemoryTrace.push_back(std::move(tensorLastReadEvent));
  }
  for (auto it = ticks.begin(); it != ticks.end(); it++) {
    if (it != ticks.begin()) {
      auto prevIt = std::prev(it);
      this->memoryContents.emplace(*it, this->memoryContents.at(*prevIt));
    } else {
      this->memoryContents.emplace(*it, std::unordered_set<TensorId>());
    }

    if (tensorWrites.find(*it) != tensorWrites.end())
      for (const auto& tensorName : tensorWrites.at(*it))
        this->memoryContents.at(*it).insert(tensorName);

    if (tensorLastReads.find(*it) != tensorLastReads.end())
      for (const auto& tensorName : tensorLastReads.at(*it))
        this->memoryContents.at(*it).erase(tensorName);

    uint64_t totalSizeBytes = 0ul;
    for (const auto& tensorName : this->memoryContents.at(*it))
      totalSizeBytes += this->tensorSize.at(tensorName);
    json memoryTimelineEvent = {
        {"name", "memoryContents"},
        {"cat", "memoryContents"},
        {"ph", "I"},
        {"ts", 1e-3 * (*it)},
        {"pid", this->sysId + 2000000ul},
        {"args", json{{"Memory", totalSizeBytes}}}};
    this->serializedMemoryTrace.push_back(std::move(memoryTimelineEvent));
    this->memoryUsage.insert(std::make_pair(*it, totalSizeBytes));
  }
}

uint64_t LocalMemUsageTracker::getPeakMemUsage() const {
  uint64_t peakMemUsage = 0ul;
  for (const auto& item : this->memoryUsage)
    peakMemUsage = std::max(peakMemUsage, item.second);
  return peakMemUsage;
}
