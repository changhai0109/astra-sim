#ifndef __LOCAL_MEM_USAGE_TRACKER__
#define __LOCAL_MEM_USAGE_TRACKER__

#include <string>
#include <unordered_map>
#include <deque>
#include "astra-sim/common/Common.hh"

namespace AstraSim {

typedef struct {
  Tick tick;
  uint64_t nodeID;
} MemActivity;

class LocalMemUsageTracker {
 public:
  LocalMemUsageTracker();
  void recordReads(const std::shared_ptr<Chakra::ETFeederNode> node, Tick tick);
  void recordWrites(
      const std::shared_ptr<Chakra::ETFeederNode> node,
      Tick tick);

 private:
  void parseIOInfos(
      const ChakraProtoMsg::AttributeProto& attr,
      std::vector<std::tuple<std::string, uint64_t>>& IOinfos);
  std::unordered_map<std::string, std::deque<MemActivity>> memReads;
  std::unordered_map<std::string, MemActivity> memWrites;
  std::unordered_map<std::string, uint64_t> tensorSize;
};

} // namespace AstraSim

#endif
