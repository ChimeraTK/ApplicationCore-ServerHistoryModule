// SPDX-FileCopyrightText: Helmholtz-Zentrum Dresden-Rossendorf, FWKE, ChimeraTK Project <chimeratk-support@desy.de>
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ServerHistory.h"

#include <ChimeraTK/ApplicationCore/ScalarAccessor.h>

#include <chrono>

namespace ChimeraTK { namespace history {

  ServerHistory::ServerHistory(ModuleGroup* owner, const std::string& name, const std::string& description,
      size_t historyLength, const std::string& historyTag, bool enableTimeStamps, const std::string& prefix,
      const std::unordered_set<std::string>& tags)
  : ApplicationModule(owner, name, description, tags), _historyLength(historyLength),
    _enbaleTimeStamps(enableTimeStamps), _prefix(prefix), _inputTag(historyTag) {
    auto model = dynamic_cast<ModuleGroup*>(_owner)->getModel();
    auto neighbourDir = model.visit(
        Model::returnDirectory, Model::getNeighbourDirectory, Model::returnFirstHit(Model::DirectoryProxy{}));
    std::vector<Model::ProcessVariableProxy> pvs;
    auto found = neighbourDir.visitByPath(".", [&](auto sourceDir) {
      sourceDir.visit([&](auto pv) { addVariableFromModel(pv); }, Model::breadthFirstSearch,
          Model::keepProcessVariables && Model::keepTag(_inputTag));
    });

    for(auto pv : pvs) {
      addVariableFromModel(pv);
    }
    if(!found) {
      throw ChimeraTK::logic_error("Path passed to BaseDAQ<TRIGGERTYPE>::addSource() not found!");
    }
    if(_overallVariableList.empty()) {
      std::cout << "ServerHistory module: No variables automatically added. This is ok if Device variables are added "
                   "by manually."
                << std::endl;
    }
  }

  void ServerHistory::addVariableFromModel(
      const Model::ProcessVariableProxy& pv, const RegisterPath& submodule, bool checkTag) {
    // gather information about the PV
    auto name = pv.getFullyQualifiedPath();
    const auto& type = pv.getNodes().front().getValueType(); // All node types must be equal for a PV
    auto length = pv.getNodes().front().getNumberOfElements();
    if(checkTag) {
      auto tag = pv.getTags();
      if(!tag.count(_inputTag)) return;
    }
    // check if qualified path name patches the given submodule name
    if(submodule != "/" && !boost::starts_with(name, std::string(submodule) + "/")) {
      return;
    }

    // check for name collision
    if(_overallVariableList.count(name) > 0) {
      throw logic_error("ServerHistory: Variable name '" + name + "' already taken.");
    }

    // create accessor and fill lists
    callForTypeNoVoid(type, [&](auto t) {
      using UserType = decltype(t);
      getAccessor<UserType>(name, length);
    });
  }

  void ServerHistory::addSource(DeviceModule& source, const std::string& submodule) {
    source.getModel().visit([&](auto pv) { addVariableFromModel(pv, submodule, false); }, Model::keepPvAccess,
        Model::adjacentSearch, Model::keepProcessVariables);
  }

  template<typename UserType>
  void ServerHistory::getAccessor(const std::string& variableName, const size_t& nElements) {
    // check if variable name already registered
    for(auto& name : _overallVariableList) {
      if(name == variableName) {
        throw logic_error("Cannot add '" + variableName +
            "' to History since a variable with that "
            "name is already registered.");
      }
    }
    _overallVariableList.insert(variableName);

    // generate name as visible in the History
    std::string historyName = RegisterPath(_prefix) / variableName;
    // add accessor and name to lists
    auto& tmpList = boost::fusion::at_key<UserType>(_accessorListMap.table);
    auto& nameList = boost::fusion::at_key<UserType>(_nameListMap.table);
    // tag to be added to the PVs created by the ServerHistory module
    std::string serverHistoryPVTag = getName();
    serverHistoryPVTag.append("_internal");
    // check if that tag is identical to the tag used to find ServerHistory vars
    if(getName().compare(_inputTag) == 0) {
      // In this case make sure to use a diffent tag name
      serverHistoryPVTag.append("_module");
    }
    if(nElements == 1) {
      tmpList.emplace_back(std::piecewise_construct,
          std::forward_as_tuple(ArrayPushInput<UserType>{this, variableName, "", 1, "", {serverHistoryPVTag}}),
          std::forward_as_tuple(HistoryEntry<UserType>{_enbaleTimeStamps}));

      // in case of a scalar history only use the variableName
      tmpList.back().second.data.emplace_back(
          ArrayOutput<UserType>{this, historyName, "", _historyLength, "", {serverHistoryPVTag}});
      if(_enbaleTimeStamps) {
        tmpList.back().second.timeStamp.emplace_back(ArrayOutput<uint64_t>{this, historyName + "_timeStamps",
            "Time stamps for entries in the history buffer", _historyLength, "", {serverHistoryPVTag}});
      }
    }
    else {
      tmpList.emplace_back(std::piecewise_construct,
          std::forward_as_tuple(ArrayPushInput<UserType>{this, variableName, "", nElements, "", {serverHistoryPVTag}}),
          std::forward_as_tuple(HistoryEntry<UserType>{_enbaleTimeStamps}));

      for(size_t i = 0; i < nElements; i++) {
        // in case of an array history append the index to the variableName
        tmpList.back().second.data.emplace_back(ArrayOutput<UserType>{
            this, historyName + "_" + std::to_string(i), "", _historyLength, "", {serverHistoryPVTag}});
        if(_enbaleTimeStamps) {
          tmpList.back().second.timeStamp.emplace_back(
              ArrayOutput<uint64_t>{this, historyName + "_" + std::to_string(i) + "_timeStamps",
                  "Time stamps for entries in the history buffer", _historyLength, "", {serverHistoryPVTag}});
        }
      }
    }
    nameList.push_back(variableName);
  }

  struct Update {
    Update(TransferElementID id) : _id(id) {}

    template<typename PAIR>
    void operator()(PAIR& pair) const {
      auto& accessorList = pair.second;
      for(auto accessor = accessorList.begin(); accessor != accessorList.end(); ++accessor) {
        if(accessor->first.getId() == _id) {
          for(size_t i = 0; i < accessor->first.getNElements(); i++) {
            std::rotate(accessor->second.data.at(i).begin(), accessor->second.data.at(i).begin() + 1,
                accessor->second.data.at(i).end());
            *(accessor->second.data.at(i).end() - 1) = accessor->first[i];
            accessor->second.data.at(i).write();
            if(accessor->second.withTimeStamps) {
              std::rotate(accessor->second.timeStamp.at(i).begin(), accessor->second.timeStamp.at(i).begin() + 1,
                  accessor->second.timeStamp.at(i).end());
              *(accessor->second.timeStamp.at(i).end() - 1) =
                  std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                      .count();
              accessor->second.timeStamp.at(i).write();
            }
          }
        }
      }
    }

    TransferElementID _id;
  };

  void ServerHistory::prepare() {
    if(!getNumberOfVariables()) {
      throw logic_error(
          "No variables are connected to the ServerHistory module. Did you use the correct tag or connect a Device?");
    }
    incrementDataFaultCounter(); // the written data is flagged as faulty
    writeAll();                  // send out initial values of all outputs.
    decrementDataFaultCounter(); // when entering the main loop calculate the validity from the inputs. No artificial increase.
  }

  void ServerHistory::mainLoop() {
    auto group = readAnyGroup();
    while(true) {
      auto id = group.readAny();
      boost::fusion::for_each(_accessorListMap.table, Update(id));
    }
  }

}} // namespace ChimeraTK::history
