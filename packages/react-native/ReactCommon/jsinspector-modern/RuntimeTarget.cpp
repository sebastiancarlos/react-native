/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <jsinspector-modern/RuntimeTarget.h>

using namespace facebook::jsi;

namespace facebook::react::jsinspector_modern {

std::shared_ptr<RuntimeTarget> RuntimeTarget::create(
    const ExecutionContextDescription& executionContextDescription,
    RuntimeTargetDelegate& delegate,
    RuntimeExecutor jsExecutor,
    VoidExecutor selfExecutor) {
  std::shared_ptr<RuntimeTarget> runtimeTarget{
      new RuntimeTarget(executionContextDescription, delegate, jsExecutor)};
  runtimeTarget->setExecutor(selfExecutor);
  return runtimeTarget;
}

RuntimeTarget::RuntimeTarget(
    const ExecutionContextDescription& executionContextDescription,
    RuntimeTargetDelegate& delegate,
    RuntimeExecutor jsExecutor)
    : executionContextDescription_(executionContextDescription),
      delegate_(delegate),
      jsExecutor_(jsExecutor) {}

std::shared_ptr<RuntimeAgent> RuntimeTarget::createAgent(
    FrontendChannel channel,
    SessionState& sessionState) {
  auto runtimeAgent = std::make_shared<RuntimeAgent>(
      channel,
      controller_,
      executionContextDescription_,
      sessionState,
      delegate_.createAgentDelegate(
          channel, sessionState, executionContextDescription_));
  agents_.insert(runtimeAgent);
  return runtimeAgent;
}

RuntimeTarget::~RuntimeTarget() {
  // Agents are owned by the session, not by RuntimeTarget, but
  // they hold a RuntimeTarget& that we must guarantee is valid.
  assert(
      agents_.empty() &&
      "RuntimeAgent objects must be destroyed before their RuntimeTarget. Did you call InstanceTarget::unregisterRuntime()?");
}

void RuntimeTarget::installBindingHandler(const std::string& bindingName) {
  jsExecutor_([bindingName,
               selfExecutor = executorFromThis()](jsi::Runtime& runtime) {
    auto globalObj = runtime.global();
    try {
      auto bindingNamePropID = jsi::PropNameID::forUtf8(runtime, bindingName);
      globalObj.setProperty(
          runtime,
          bindingNamePropID,
          jsi::Function::createFromHostFunction(
              runtime,
              bindingNamePropID,
              1,
              [bindingName, selfExecutor](
                  jsi::Runtime& rt,
                  const jsi::Value&,
                  const jsi::Value* args,
                  size_t count) -> jsi::Value {
                if (count != 1 || !args[0].isString()) {
                  throw jsi::JSError(
                      rt, "Invalid arguments: should be exactly one string.");
                }
                std::string payload = args[0].getString(rt).utf8(rt);

                selfExecutor([bindingName, payload](auto& self) {
                  self.agents_.forEach([bindingName, payload](auto& agent) {
                    agent.notifyBindingCalled(bindingName, payload);
                  });
                });

                return jsi::Value::undefined();
              }));
    } catch (jsi::JSError&) {
      // Per Chrome's implementation, @cdp Runtime.createBinding swallows
      // JavaScript exceptions that occur while setting up the binding.
    }
  });
}

RuntimeTargetController::RuntimeTargetController(RuntimeTarget& target)
    : target_(target) {}

void RuntimeTargetController::installBindingHandler(
    const std::string& bindingName) {
  target_.installBindingHandler(bindingName);
}

} // namespace facebook::react::jsinspector_modern
