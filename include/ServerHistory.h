// SPDX-FileCopyrightText: Helmholtz-Zentrum Dresden-Rossendorf, FWKE, ChimeraTK Project <chimeratk-support@desy.de>
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

/*!
 * \author Klaus Zenker (HZDR)
 * \date 10.08.2018
 * \page historydoc Server based history module
 * \section historyintro Server based history
 *  Some control systems offer a variable history but some do not. In this case
 * the \c ServerHistory can be used to create a history ring buffer managed by the
 * server. If only a local history is needed consider to use the \c MicroDAQ
 * module.
 * In order use the Server history create a \c ServerHistory module.
 * All variables that have the correct tag (default: "history") will be added to the Server history.
 * The history length is set during module construction and fixed per module.
 * Every time one of the variable handled by the history module is updated it will be filled into the
 * history buffer. The buffer length (history length) can not be changed during
 * runtime. Finally, one can create an additional buffer for each history
 * buffer that includes the time stamps of each data point in the history buffer.
 * This is useful if not all history buffers are filled with the same rate or the
 * rate is not known.
 *
 *
 *  Output variables created by the \c ServerHistory module are named like their
 * feeding process variables with a prefixed name that is set when the process
 * variables is added to the history module. In case of Array type feeding
 * process variables n history buffers are created (where n is the Array size)
 * and the element index i is appended to the feeding process variable name. In
 * consequence an input array of length i will result in i output history
 * arrays. The following tags are added to the history output variable:
 *  - Name of the history module with the suffix "_internal" appended
 *
 * The connection of variables with the 'history' tag to the ServerHistory module is
 * done automatically.
 * \attention Only variables of modules defined before constructing the ServerHistory
 * module are considered.
 *
 * It is also possible to connect a DeviceModule to the ServerHistory module.
 * Variables of Devices have no tags and therefor they will not be automatically connected
 * to the SereverHistory module. One has to call addSource().
 * In addition a trigger in case the variables are not push type. It has to be set when
 * constructing the DeviceModule.
 * If the device variables are writable they are of push type. In this case
 * the trigger will not be added. One has to use the LogicalNameMapping backend to
 * force the device variables to be read only by using the \c forceReadOnly plugin.
 * Using the LogicalNameMapping backend also allows to select individual device
 * process variables to be connected to the \c ServerHistory.
 *
 *
 *  The following example shows how to integrate the \c ServerHistory module.
 *  \code
 *  sruct TestModule: public ChimeraTK::ApplicationModule{
 *  chimeraTK::ScalarOutput<float> measurement{this, "measurement", "" ,
 * "measurement variable", {"history"}};
 *  ...
 *  };
 *  struct myApp : public ChimeraTK::Application{
 *
 *  history::ServerHistory history{this, "ServerHistory", "History module", 20}; // History buffer length is 20
 *
 *  ChimeraTK::DeviceModule dev{this, "Dummy", "Trigger/tick"};
 *
 *
 *
 *  TestModule test{ this, "test", "" };
 *  ...
 *  };
 *
 *
 *  void myAPP::myAPP(){
 *  // The variable of the TestModule will show up in the control system as history/test/measurement automatically
 * (identified by the tag).
 *  // Add a device. Updating of the history buffer is trigger external by the given trigger
 *  history.addSource(&ev);
 *  ...
 *  }
 *
 *  \endcode
 *
 *  \remark Before starting the main loop of the server history module \c readAnyGroup() is called.
 *  This seems to block until all connected variables are written once. So if the history buffers
 *  are not filled make sure all variables are written. If they are not written in the module main loop,
 *  write them once before the main loop of the module containing the history variables.
 */

#include <unordered_set>

#include <ChimeraTK/ApplicationCore/ApplicationModule.h>
#include <ChimeraTK/ApplicationCore/ArrayAccessor.h>
#include <ChimeraTK/ApplicationCore/DeviceModule.h>
#include <ChimeraTK/ApplicationCore/VariableGroup.h>
#include <ChimeraTK/SupportedUserTypes.h>

#include <string>
#include <tuple>
#include <vector>

namespace ChimeraTK { namespace history {

  //  struct AccessorAttacher;

  template<typename UserType>
  struct HistoryEntry {
    HistoryEntry(bool enableHistory)
    : data(std::vector<ArrayOutput<UserType>>{}), timeStamp(std::vector<ArrayOutput<uint64_t>>{}),
      withTimeStamps(enableHistory) {}
    std::vector<ArrayOutput<UserType>> data;
    std::vector<ArrayOutput<uint64_t>> timeStamp;
    bool withTimeStamps;
  };

  class ServerHistory : public ApplicationModule {
   public:
    /**
     * Constructor.
     * Addition parameters to a normal application module constructor:
     * \param owner Owning module passed to ApplicationModule constructor.
     * \param name Module name passed to ApplicationModule constructor.
     * \param description Module description passed to ApplicationModule constructor.
     * \param historyLength Length of the history buffers.
     * \param historyTag The tag used to identify PVs to be considered in the server history module.
     * \param enableTimeStamps An additional ring buffer per variable will be added that holds the time stamps
     * corresponding to the data ring buffer entries. \param prefix A prefix specifying the directory were history PVs
     * are put. \param tags Module tags passed to ApplicationModule constructor.
     */
    ServerHistory(ModuleGroup* owner, const std::string& name, const std::string& description,
        size_t historyLength = 1200, const std::string& historyTag = "history", bool enableTimeStamps = false,
        const std::string& prefix = "History", const std::unordered_set<std::string>& tags = {});

    /** Default constructor, creates a non-working module. Can be used for late
     * initialisation. */
    ServerHistory() {}

    /**
     * Ad variables of a device to the ServerHistory. Calls virtualiseFromCatalog to get access to the internal variables.
     *
     * \param source For all variables of this module ring buffers are created.
     *               Use the LogicalNameMapping to create a virtual device module that holds all variables that should be
     *               passed to the history module.
     * \param submodule If only a submodule should be added give the name.
     *                  It does not work do create a submodule of the DeviceModule itself!
     */
    void addSource(DeviceModule& source, const std::string& submodule = "");

    void prepare() override;
    void mainLoop() override;

    /*
     * Helper function used in tests.
     * return Number of variables added to the history server module.
     */
    size_t getNumberOfVariables() { return _overallVariableList.size(); }

   private:
    void addVariableFromModel(
        const ChimeraTK::Model::ProcessVariableProxy& pv, const RegisterPath& submodule = "", bool checkTag = true);

    template<typename UserType>
    void getAccessor(const std::string& variableName, const size_t& nElements);

    /** boost::fusion::map of UserTypes to std::lists containing the
     * ArrayPushInput and ArrayOutput accessors. These accessors are dynamically
     * created by the AccessorAttacher. */
    template<typename UserType>
    using AccessorList = std::list<std::pair<ArrayPushInput<UserType>, HistoryEntry<UserType>>>;
    TemplateUserTypeMapNoVoid<AccessorList> _accessorListMap;

    /** boost::fusion::map of UserTypes to std::lists containing the names of the
     * accessors. Technically there would be no need to use TemplateUserTypeMap
     * for this (as type does not depend on the UserType), but since these lists
     * must be filled consistently with the accessorListMap, the same construction
     * is used here. */
    template<typename UserType>
    using NameList = std::list<std::string>;
    TemplateUserTypeMapNoVoid<NameList> _nameListMap;

    /** Overall variable name list, used to detect name collisions */
    std::set<std::string> _overallVariableList;

    size_t _historyLength{1200};      ///< Length of the ring buffers used by the server history module
    bool _enbaleTimeStamps{false};    ///< If enabled addition ring buffers for time stamps are created
    std::string _prefix{"History"};   ///< The prefix will determine the directory where server history PVs appear
    std::string _inputTag{"history"}; ///< The tag used to identify server history PVs
  };
}} // namespace ChimeraTK::history
