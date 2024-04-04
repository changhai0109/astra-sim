#include "astra-sim/workload/LocalMemUsageTracker.hh"

#include <cassert>
#include "astra-sim/common/Common.hh"

using namespace AstraSim;
using namespace Chakra;

LocalMemUsageTracker::LocalMemUsageTracker() {}

uint64_t LocalMemUsageTracker::parseIOInfos(
    const ChakraProtoMsg::AttributeProto& attr,
    std::vector<std::tuple<std::string, uint64_t>>& IOinfos) {
  assert(IOinfos.size() == 0ul);
  assert(attr.string_list().values().size() % 2 == 0);
  uint64_t parsed_cnt = 0ul;
  for (size_t i = 0; i < attr.string_list().values().size(); i += 2) {
    std::string name = attr.string_list(i);
    std::string sizeStr = attr.string_list(i + 1);
    uint64_t size = std::stoull(sizeStr);
    IOinfos.push_back(std::make_tuple(name, size));
    parsed_cnt++;
  }
  return parsed_cnt;
}

void LocalMemUsageTracker::recordReads(
    const std::shared_ptr<ChakraETFeederNode> node,
    Tick tick) {
  static std::vector<std::tuple<std::string, uint64_t>> IOinfos;
  IOinfos.clear();
  uint64_t nodeId = node->id();
  if (!node->has_other_attr("inputs"))
    throw MissingAttrException(
        "inputs", nodeId, "LocalMemUsageTracker::recordReads");
  auto inputsAttr = node->get_other_attr("inputs");
  this->parseIOInfos(inputsAttr, IOinfos);
  for (auto iter : IOinfos) {
    std::string tensorName = iter.first;
    uint64_t tensorSize = iter.second;
    MemActivity readActivity;
    readActivity.tick = tick;
    readActivity.nodeID = nodeId;
    if (this->tensorSize.find(tensorName) == this->tensorSize.end()) {
      // TODO: read before write, for now assuming it exist from start.
      MemActivity writeActivity;
      writeActivity.tick = 0ul;
      writeActivity.nodeID = __UINT64_MAX__;
      this->tensorSize.insert({tensorName, tensorSize});
      this->memWrites.insert({tensorName, writeActivity});
    }
    if (this->memReads.find(tensorName) == this->memReads.end())
      this->memReads.emplace(tensorName, std::deque<MemActivity>);
    this->memReads[tensorName].emplace_back(readActivity);
  }
}

void LocalMemUsageTracker::recordWrites(
    const std::shared_ptr<ChakraETFeederNode> node,
    Tick tick) {
  static std::vector<std::tuple<std::string, uint64_t>> IOinfos;
  IOinfos.clear();
  uint64_t nodeId = node->id();
  if (!node->has_other_attr("outputs"))
    throw MissingAttrException(
        "outputs", nodeId, "LocalMemUsageTracker::recordWrites");

  auto outputsAttr = node->get_other_attr("outputs");
  this->parseIOInfos(outputsAttr, IOinfos);
  for (auto iter : IOinfos) {
    std::string tensorName = iter.first;
    uint64_t tensorSize = iter.second;
    MemActivity writeActivity;
    writeActivity.tick = tick;
    writeActivity.nodeID = nodeId;
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
