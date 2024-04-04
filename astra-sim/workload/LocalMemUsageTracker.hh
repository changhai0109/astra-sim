#ifndef __LOCAL_MEM_USAGE_TRACKER__
#define __LOCAL_MEM_USAGE_TRACKER__

#include <json/json.hpp>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include "astra-sim/common/Common.hh"
#include "extern/graph_frontend/chakra/et_feeder/et_feeder.h"

using json = nlohmann::json;

namespace AstraSim {

typedef struct {
  Tick start;
  Tick end;
  std::shared_ptr<Chakra::ETFeederNode> node;
} MemActivity;

class LocalMemUsageTracker {
 public:
  LocalMemUsageTracker(uint64_t sysId) : sysId(sysId){};
  void recordStart(const std::shared_ptr<Chakra::ETFeederNode> node, Tick tick);
  void recordEnd(const std::shared_ptr<Chakra::ETFeederNode> node, Tick tick);
  void buildMemoryTrace();
  void dumpMemoryTrace(const std::string& filename);
  uint64_t sysId;

 private:
  void recordReads(
      const std::shared_ptr<Chakra::ETFeederNode> node,
      Tick start,
      Tick end);
  void recordWrites(
      const std::shared_ptr<Chakra::ETFeederNode> node,
      Tick start,
      Tick end);
  uint64_t parseIOInfos(
      const ChakraProtoMsg::AttributeProto& attr,
      std::vector<std::tuple<std::string, uint64_t>>& IOinfos);
  std::unordered_map<std::string, std::deque<MemActivity>> memReads;
  std::unordered_map<std::string, MemActivity> memWrites;
  std::unordered_map<std::string, uint64_t> tensorSize;
  std::unordered_map<std::shared_ptr<Chakra::ETFeederNode>, Tick>
      activityStartTime;
  std::vector<json> serializedMemoryTrace;
};

} // namespace AstraSim

#endif
