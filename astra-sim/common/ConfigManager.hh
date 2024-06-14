#ifndef __COMMON_CONFIG_MANAGER_HH__
#define __COMMON_CONFIG_MANAGER_HH__

#include <iostream>
#include <string>
#include "json/json.hpp"
#include "yaml-cpp/yaml.h"

using json = nlohmann::json;

namespace AstraSim {

class ConfigManager {
 public:
  static ConfigManager* getInstance();

  void addJsonConfig(const json& json, const std::string& scope = "");

  void addJsonConfigFile(
      const std::string& filename,
      const std::string& scope = "");

  void addYamlConfigFile(
      const std::string& filename,
      const std::string& scope = "");

  const ConfigManager getSubScope(const std::string& scopeName) const;

  template <typename T>
  T get(const std::string& name) const {
    auto factoredName = this->factorName(name);
    auto scope = factoredName.first;
    auto subname = factoredName.second;
    if (!this->config.contains(scope))
      throw std::invalid_argument("");
    if (subname.empty())
      return this->config.at(scope);

    auto subConfig = this->getSubScope(scope);
    return subConfig.template get<T>(subname);
  }

  const json& getRawJson() const;

 private:
  json config;
  static ConfigManager* instance;
  static void cleanup();
  std::pair<std::string, std::string> factorName(const std::string& name) const;
  ConfigManager() {
    this->config = json::object({{"_comment", "astrasim config"}});
  }
  ConfigManager(json config);

  json _yamlParseScalar(const YAML::Node& node) const;
  json _yamlToJson(const YAML::Node& root) const;
};

} // namespace AstraSim

#endif
