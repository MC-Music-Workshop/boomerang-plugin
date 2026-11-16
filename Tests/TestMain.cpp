#include "TestHelpers.h"
#include <thread>

// Runs every registered juce::UnitTest and exits non-zero if any assertion failed.
//
// This is a JUCE application rather than a plain main() so that a real message
// loop runs on the main thread while the tests run on a worker thread. The
// processor reports state changes back to its host parameters with
// MessageManager::callAsync; without a live message loop those echoes are never
// delivered and the parameter-driven tests wouldn't see what a host sees.
class TestApplication : public juce::JUCEApplicationBase
{
public:
  const juce::String getApplicationName() override    { return "BoomerangTests"; }
  const juce::String getApplicationVersion() override { return BOOMERANG_VERSION; }
  bool moreThanOneInstanceAllowed() override          { return true; }

  void initialise(const juce::String&) override
  {
   #if JUCE_MAC
    juce::Process::setDockIconVisible(false);
   #endif

    worker = std::thread([this]
    {
      juce::UnitTestRunner runner;
      runner.setAssertOnFailure(false);
      runner.runAllTests();

      int num_failures = 0;
      for (int i = 0; i < runner.getNumResults(); ++i)
        num_failures += runner.getResult(i)->failures;

      std::cout << (num_failures == 0 ? "All tests passed" : "FAILED assertions: " + std::to_string(num_failures)) << std::endl;

      juce::MessageManager::callAsync([this, num_failures]
      {
        setApplicationReturnValue(num_failures == 0 ? 0 : 1);
        quit();
      });
    });
  }

  void shutdown() override
  {
    if (worker.joinable())
      worker.join();
  }

  void anotherInstanceStarted(const juce::String&) override {}
  void systemRequestedQuit() override                       { quit(); }
  void suspended() override                                 {}
  void resumed() override                                   {}
  void unhandledException(const std::exception*, const juce::String&, int) override {}

private:
  std::thread worker;
};

// START_JUCE_APPLICATION refuses to compile against plugin code, so this is
// the same thing it expands to for a console program
static juce::JUCEApplicationBase* createTestApplication() { return new TestApplication(); }

int main(int argc, char* argv[])
{
  juce::JUCEApplicationBase::createInstance = &createTestApplication;
  return juce::JUCEApplicationBase::main(argc, (const char**) argv);
}
