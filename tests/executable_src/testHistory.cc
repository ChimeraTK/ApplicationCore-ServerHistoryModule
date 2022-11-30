// SPDX-FileCopyrightText: Helmholtz-Zentrum Dresden-Rossendorf, FWKE, ChimeraTK Project <chimeratk-support@desy.de>
// SPDX-License-Identifier: LGPL-3.0-or-later
#define BOOST_TEST_MODULE HistoryTest

#include "ServerHistory.h"

#include <ChimeraTK/ApplicationCore/ScalarAccessor.h>
#include <ChimeraTK/ApplicationCore/TestFacility.h>

#include <boost/mpl/list.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <fstream>

using namespace boost::unit_test_framework;

// list of user types the accessors are tested with
typedef boost::mpl::list<int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, float, double, std::string> test_types;

template<typename TestType>
TestType getNumber(double val) {
  return (TestType)val;
}

template<>
std::string getNumber<std::string>(double val) {
  return std::to_string(val);
}

template<typename UserType>
struct Dummy : public ChimeraTK::ApplicationModule {
  using ApplicationModule::ApplicationModule;
  ChimeraTK::ScalarPushInput<UserType> in{this, "in", "", "Dummy input"};
  ChimeraTK::ScalarOutput<UserType> out{this, "out", "", "Dummy output", {"history"}};

  void mainLoop() override {
    while(true) {
      // write first so initial values are propagated
      out = static_cast<UserType>(in);
      out.write();
      in.read(); // read at the end of the loop
    }
  }
};

template<typename UserType>
struct DummyArray : public ChimeraTK::ApplicationModule {
  using ChimeraTK::ApplicationModule::ApplicationModule;
  ChimeraTK::ArrayPushInput<UserType> in{this, "in", "", 3, "Dummy input"};
  ChimeraTK::ArrayOutput<UserType> out{this, "out", "", 3, "Dummy output", {"history"}};

  void mainLoop() override {
    while(true) {
      for(unsigned int i = 0; i < 3; i++) {
        out[i] = in[i];
      }
      out.write();
      in.read();
    }
  }
};

/**
 * Define a test app to test the scalar History Module.
 */
template<typename UserType>
struct testApp : public ChimeraTK::Application {
  testApp(const std::string& historyTag = "history") : Application("test") {
    hist = ChimeraTK::history::ServerHistory{
        this, "historyTest", "History of selected process variables.", 20, historyTag};
  }
  ~testApp() override { shutdown(); }

  Dummy<UserType> dummy{this, "Dummy", "Dummy module"};
  // do not use name history here - else output vars will be added to the history too
  ChimeraTK::history::ServerHistory hist{this, "history", "History of selected process variables.", 20};
};

/**
 * Define a test app to test the array History Module.
 */
template<typename UserType>
struct testAppArray : public ChimeraTK::Application {
  testAppArray() : Application("test") {}
  ~testAppArray() override { shutdown(); }

  DummyArray<UserType> dummy{this, "Dummy", "Dummy module"};
  ChimeraTK::history::ServerHistory hist{this, "history", "History of selected process variables.", 20};
};

/**
 * Define a test app to test the device module in combination with the History Module.
 */
struct testAppDev : public ChimeraTK::Application {
  testAppDev() : Application("test") { hist.addSource(dev, ""); }
  ~testAppDev() override { shutdown(); }

  // Set dmap file before creating DeviceModules
  ChimeraTK::SetDMapFilePath dmap{"test.dmap"};
  // Set trigger to Dummy.out
  ChimeraTK::DeviceModule dev{this, "Dummy1Mapped", "/Dummy/out"};

  DummyArray<int> dummy{this, "Dummy", "Dummy module"};

  ChimeraTK::history::ServerHistory hist{
      this, "history", "History of selected process variables.", 20, "history", false};
};

BOOST_AUTO_TEST_CASE(testNoVarsFound) {
  std::puts("testNoVarsFound");
  testApp<int> app{"History"};
  BOOST_CHECK_EQUAL(app.hist.getNumberOfVariables(), 0);
  ChimeraTK::TestFacility tf(app);
  BOOST_CHECK_THROW(tf.runApplication(), ChimeraTK::logic_error);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(testScalarHistory, T, test_types) {
  std::cout << "testScalarHistory " << typeid(T).name() << std::endl;
  testApp<T> app;
  ChimeraTK::TestFacility tf(app);
  BOOST_CHECK_EQUAL(app.hist.getNumberOfVariables(), 1);
  auto i = tf.getScalar<T>("Dummy/in");
  tf.runApplication();
  i = getNumber<T>(42.);
  i.write();
  tf.stepApplication();
  std::vector<T> v_ref(20);
  v_ref.back() = getNumber<T>(42.);
  auto v = tf.readArray<T>("History/Dummy/out");
  BOOST_CHECK_EQUAL_COLLECTIONS(v.begin(), v.end(), v_ref.begin(), v_ref.end());
  i = getNumber<T>(42.);
  i.write();
  tf.stepApplication();
  *(v_ref.end() - 2) = getNumber<T>(42.);
  v = tf.readArray<T>("History/Dummy/out");
  BOOST_CHECK_EQUAL_COLLECTIONS(v.begin(), v.end(), v_ref.begin(), v_ref.end());
}

BOOST_AUTO_TEST_CASE_TEMPLATE(testArrayHistory, T, test_types) {
  std::cout << "testArrayHistory " << typeid(T).name() << std::endl;
  testAppArray<T> app;
  ChimeraTK::TestFacility tf(app);
  BOOST_CHECK_EQUAL(app.hist.getNumberOfVariables(), 1);
  auto arr = tf.getArray<T>("Dummy/in");
  tf.runApplication();
  arr[0] = getNumber<T>(42.);
  arr[1] = getNumber<T>(43.);
  arr[2] = getNumber<T>(44.);
  arr.write();
  tf.stepApplication();
  BOOST_CHECK_EQUAL(tf.readArray<T>("Dummy/out")[0], getNumber<T>(42.));
  BOOST_CHECK_EQUAL(tf.readArray<T>("Dummy/out")[1], getNumber<T>(43.));
  BOOST_CHECK_EQUAL(tf.readArray<T>("Dummy/out")[2], getNumber<T>(44.));
  std::vector<T> v_ref(20);
  for(size_t i = 0; i < 3; i++) {
    v_ref.back() = getNumber<T>(42.0 + i);
    auto v = tf.readArray<T>("History/Dummy/out_" + std::to_string(i));
    BOOST_CHECK_EQUAL_COLLECTIONS(v.begin(), v.end(), v_ref.begin(), v_ref.end());
  }

  arr[0] = getNumber<T>(1.0);
  arr[1] = getNumber<T>(2.0);
  arr[2] = getNumber<T>(3.0);
  arr.write();
  tf.stepApplication();
  for(size_t i = 0; i < 3; i++) {
    *(v_ref.end() - 2) = getNumber<T>(42.0 + i);
    *(v_ref.end() - 1) = getNumber<T>(1.0 + i);
    auto v = tf.readArray<T>("History/Dummy/out_" + std::to_string(i));
    BOOST_CHECK_EQUAL_COLLECTIONS(v.begin(), v.end(), v_ref.begin(), v_ref.end());
  }
}

BOOST_AUTO_TEST_CASE(testDeviceHistory) {
  std::cout << "testDeviceHistory" << std::endl;
  testAppDev app;
  ChimeraTK::TestFacility tf(app);

  // We use this device directly to change its values
  ChimeraTK::Device dev;
  // Use Dummy1 to change device values, since Dummy1Mapped is read only
  dev.open("Dummy1");
  dev.write("/FixedPoint/value", 42);

  // Trigger the reading of the device
  auto i = tf.getScalar<int>("Dummy/in");
  //  BOOST_CHECK(true);
  tf.runApplication();
  i = 1.;
  i.write();

  tf.stepApplication();

  // check new history buffer that ends with 42
  std::vector<double> v_ref(20);
  v_ref.back() = 42;
  auto v = tf.readArray<float>("History/Device/signed32");
  BOOST_CHECK_EQUAL_COLLECTIONS(v.begin(), v.end(), v_ref.begin(), v_ref.end());

  // Trigger the reading of the device
  i = 1.;
  i.write();

  tf.stepApplication();

  // check new history buffer that ends with 42,42
  *(v_ref.end() - 2) = 42;
  v = tf.readArray<float>("History/Device/signed32");
  BOOST_CHECK_EQUAL_COLLECTIONS(v.begin(), v.end(), v_ref.begin(), v_ref.end());

  dev.write("/FixedPoint/value", 43);

  // Trigger the reading of the device
  i = 1.;
  i.write();

  tf.stepApplication();

  // check new history buffer that ends with 42,42,43
  *(v_ref.end() - 1) = 43;
  *(v_ref.end() - 3) = 42;
  v = tf.readArray<float>("History/Device/signed32");
  BOOST_CHECK_EQUAL_COLLECTIONS(v.begin(), v.end(), v_ref.begin(), v_ref.end());
}
