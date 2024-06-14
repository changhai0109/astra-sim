#include "astra-sim/common/ConfigManager.hh"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include "json/json.hpp"
#include "yaml-cpp/yaml.h"

using namespace AstraSim;
using json = nlohmann::json;

ConfigManager* ConfigManager::instance = nullptr;

ConfigManager* ConfigManager::getInstance() {
  if (ConfigManager::instance == nullptr) {
    ConfigManager::instance = new ConfigManager();
    atexit(ConfigManager::cleanup);
  }
  return ConfigManager::instance;
}

void ConfigManager::addJsonConfig(const json& j, const std::string& scope) {
  json::object_t* target = this->config.get_ptr<json::object_t*>();
  json::object_t* target2 = nullptr;
  std::string fullName = scope;
  while (true) {
    if (fullName.empty())
      break;
    auto factoredName = this->factorName(fullName);
    if (target->find(factoredName.first) == target->end())
      target->insert({{factoredName.first, json{}}});
    target2 = target;
    target = target->at(factoredName.first).get_ptr<json::object_t*>();
    if (factoredName.second.empty())
      break;
    fullName = factoredName.second;
  }
  target2->at(fullName) = j;
}

void ConfigManager::addJsonConfigFile(
    const std::string& filename,
    const std::string& scope) {
  json j{};
  std::ifstream inFile;
  inFile.open(filename);
  inFile >> j;
  inFile.close();
  this->addJsonConfig(j, scope);
}

void ConfigManager::addYamlConfigFile(
    const std::string& filename,
    const std::string& scope) {
  YAML::Node yamlData = YAML::LoadFile(filename);
  json j = this->_yamlToJson(yamlData);
  this->addJsonConfig(j, scope);
}

json ConfigManager::_yamlParseScalar(const YAML::Node& node) const {
  int i;
  double d;
  bool b;
  std::string s;

  if (YAML::convert<int>::decode(node, i))
    return i;
  if (YAML::convert<double>::decode(node, d))
    return d;
  if (YAML::convert<bool>::decode(node, b))
    return b;
  if (YAML::convert<std::string>::decode(node, s))
    return s;

  return nullptr;
}

json ConfigManager::_yamlToJson(const YAML::Node& root) const {
  json j{};

  switch (root.Type()) {
    case YAML::NodeType::Null:
      break;
    case YAML::NodeType::Scalar:
      return _yamlParseScalar(root);
    case YAML::NodeType::Sequence:
      for (auto&& node : root)
        j.emplace_back(_yamlToJson(node));
      break;
    case YAML::NodeType::Map:
      for (auto&& it : root)
        j[it.first.as<std::string>()] = _yamlToJson(it.second);
      break;
    default:
      break;
  }
  return j;
}

void ConfigManager::cleanup() {
  delete ConfigManager::instance;
}

const ConfigManager ConfigManager::getSubScope(
    const std::string& scopeName) const {
  auto factoredName = this->factorName(scopeName);
  auto scope = factoredName.first;
  auto name = factoredName.second;
  if (!this->config.contains(scope)) {
    throw std::invalid_argument("");
  }
  auto subConfig = ConfigManager(this->config[scope]);
  if (!name.empty()) {
    return subConfig.getSubScope(name);
  }
  return subConfig;
}

std::pair<std::string, std::string> ConfigManager::factorName(
    const std::string& name) const {
  auto pos = name.find(".");
  if (pos != std::string::npos) {
    std::string scope = name.substr(0, pos);
    std::string nextName = name.substr(pos + 1);
    return std::make_pair(scope, nextName);
  }
  return std::make_pair(name, "");
}

ConfigManager::ConfigManager(json config) {
  this->config = config;
}

const json& ConfigManager::getRawJson() const {
  return this->config;
}
