/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "FabricMountingManagerTestHelper.h"

#include <react/fabric/FabricUIManagerBinding.h>
#include <react/renderer/animationbackend/AnimatedProps.h>
#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/components/root/RootShadowNode.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ComponentDescriptor.h>
#include <react/renderer/mounting/MountingOverrideDelegate.h>
#include <react/renderer/mounting/ShadowView.h>
#include <react/renderer/scheduler/Scheduler.h>

namespace facebook::react {

class FabricMountingManagerTestHelper::DeleteRecorder
    : public MountingOverrideDelegate {
 public:
  bool shouldOverridePullTransaction() const override {
    return true;
  }

  std::optional<MountingTransaction> pullTransaction(
      SurfaceId surfaceId,
      MountingTransaction::Number number,
      const TransactionTelemetry& telemetry,
      ShadowViewMutationList mutations) const override {
    for (const auto& mutation : mutations) {
      if (mutation.type == ShadowViewMutation::Delete) {
        deletedTags.push_back(mutation.oldChildShadowView.tag);
      }
    }
    return MountingTransaction{
        surfaceId, number, std::move(mutations), telemetry};
  }

  mutable std::vector<Tag> deletedTags;
};

namespace {

void runNoJavaScript(std::function<void(jsi::Runtime& runtime)>&& /*unused*/) {}

std::vector<Tag> toTags(jni::alias_ref<jni::JArrayInt> tags) {
  auto region = tags->getRegion(0, static_cast<jsize>(tags->size()));
  return {region.get(), region.get() + tags->size()};
}

} // namespace

FabricMountingManagerTestHelper::FabricMountingManagerTestHelper(
    jni::alias_ref<JFabricUIManager::javaobject> jFabricUIManager)
    : javaUIManager_(jni::make_global(jFabricUIManager)) {
  mountingManager_ = std::make_shared<FabricMountingManager>(javaUIManager_);

  auto params = ComponentDescriptorParameters{
      .eventDispatcher = EventDispatcher::Shared{},
      .contextContainer = std::make_shared<ContextContainer>(),
      .flavor = nullptr};
  viewComponentDescriptor_ = std::make_unique<ViewComponentDescriptor>(params);
}

void FabricMountingManagerTestHelper::initHybrid(
    jni::alias_ref<jhybridobject> jobj,
    jni::alias_ref<JFabricUIManager::javaobject> jFabricUIManager) {
  setCxxInstance(jobj, jFabricUIManager);
}

void FabricMountingManagerTestHelper::startSurface(jint surfaceId) {
  mountingManager_->onSurfaceStart(surfaceId);
}

void FabricMountingManagerTestHelper::stopSurface(jint surfaceId) {
  mountingManager_->onSurfaceStop(surfaceId);
}

void FabricMountingManagerTestHelper::preallocateView(
    jint surfaceId,
    jint tag) {
  ShadowView sv{};
  sv.componentName = "View";
  sv.surfaceId = surfaceId;
  sv.tag = tag;
  sv.props = std::make_shared<const ViewProps>();
  sv.layoutMetrics.frame.size = {.width = 100, .height = 100};
  mountingManager_->preallocateShadowView(sv);
}

void FabricMountingManagerTestHelper::destroyUnmountedView(
    jint surfaceId,
    jint tag) {
  auto family = viewComponentDescriptor_->createFamily(
      {.tag = tag, .surfaceId = surfaceId, .instanceHandle = nullptr});
  mountingManager_->destroyUnmountedShadowNode(*family);
}

bool FabricMountingManagerTestHelper::isTagAllocated(jint surfaceId, jint tag) {
  return mountingManager_->isViewAllocated(surfaceId, tag);
}

void FabricMountingManagerTestHelper::synchronouslyUpdateAnimatedProps(
    jni::alias_ref<jni::JArrayInt> tags,
    jni::alias_ref<jni::JArrayClass<NativeMap::javaobject>> props) {
  auto tagValues = tags->getRegion(0, static_cast<jsize>(tags->size()));
  std::unordered_map<Tag, AnimatedProps> updates;
  for (size_t i = 0; i < tags->size(); i++) {
    updates.emplace(
        tagValues[i],
        AnimatedProps{
            .props = {},
            .rawProps = std::make_unique<RawProps>(
                props->getElement(i)->cthis()->consume())});
  }
  mountingManager_->synchronouslyUpdateAnimatedProps(updates);
}

jni::local_ref<JRuntimeExecutor::jhybridobject>
FabricMountingManagerTestHelper::createRuntimeExecutor() {
  return JRuntimeExecutor::newObjectCxxArgs(runNoJavaScript);
}

jni::local_ref<JRuntimeScheduler::jhybridobject>
FabricMountingManagerTestHelper::createRuntimeScheduler() {
  // Work scheduled here never runs and stays queued. A queued task holds the
  // UIManager, which holds this scheduler, so both outlive the test.
  runtimeScheduler_ = std::make_shared<RuntimeScheduler>(runNoJavaScript);
  return JRuntimeScheduler::newObjectCxxArgs(runtimeScheduler_);
}

void FabricMountingManagerTestHelper::registerViewComponent(
    ComponentFactory* componentFactory) {
  componentFactory->buildRegistryFunction =
      [](const EventDispatcher::Weak& eventDispatcher,
         const std::shared_ptr<const ContextContainer>& contextContainer) {
        auto providerRegistry =
            std::make_shared<ComponentDescriptorProviderRegistry>();
        providerRegistry->add(
            concreteComponentDescriptorProvider<ViewComponentDescriptor>());
        return providerRegistry->createComponentDescriptorRegistry(
            {.eventDispatcher = eventDispatcher,
             .contextContainer = contextContainer,
             .flavor = nullptr});
      };
}

void FabricMountingManagerTestHelper::commitViews(
    jint surfaceId,
    jni::alias_ref<jni::JArrayInt> tags) {
  auto children =
      std::make_shared<std::vector<std::shared_ptr<const ShadowNode>>>();
  for (auto tag : toTags(tags)) {
    auto props = std::make_shared<ViewProps>();
    props->collapsable = false;
    children->push_back(viewComponentDescriptor_->createShadowNode(
        {.props = props},
        viewComponentDescriptor_->createFamily(
            {.tag = tag, .surfaceId = surfaceId, .instanceHandle = nullptr})));
  }
  javaUIManager_->getBinding()
      ->getScheduler()
      ->getUIManager()
      ->getShadowTreeRegistry()
      .visit(surfaceId, [&](const ShadowTree& shadowTree) {
        shadowTree.commit(
            [&](const RootShadowNode& oldRootShadowNode) {
              return std::make_shared<RootShadowNode>(
                  oldRootShadowNode,
                  ShadowNodeFragment{
                      .props = ShadowNodeFragment::propsPlaceholder(),
                      .children = children});
            },
            {});
      });
}

void FabricMountingManagerTestHelper::recordPulledDeletes(jint surfaceId) {
  deleteRecorder_ = std::make_shared<DeleteRecorder>();
  javaUIManager_->getBinding()
      ->getScheduler()
      ->getUIManager()
      ->getShadowTreeRegistry()
      .visit(surfaceId, [&](const ShadowTree& shadowTree) {
        shadowTree.getMountingCoordinator()->setMountingOverrideDelegate(
            deleteRecorder_);
      });
}

jni::local_ref<jni::JArrayInt>
FabricMountingManagerTestHelper::pulledDeleteTags() {
  const auto& tags = deleteRecorder_->deletedTags;
  auto array = jni::JArrayInt::newArray(tags.size());
  array->setRegion(0, static_cast<jsize>(tags.size()), tags.data());
  return array;
}

void FabricMountingManagerTestHelper::registerNatives() {
  registerHybrid({
      makeNativeMethod(
          "initHybrid", FabricMountingManagerTestHelper::initHybrid),
      makeNativeMethod(
          "startSurface", FabricMountingManagerTestHelper::startSurface),
      makeNativeMethod(
          "stopSurface", FabricMountingManagerTestHelper::stopSurface),
      makeNativeMethod(
          "preallocateView", FabricMountingManagerTestHelper::preallocateView),
      makeNativeMethod(
          "destroyUnmountedView",
          FabricMountingManagerTestHelper::destroyUnmountedView),
      makeNativeMethod(
          "isTagAllocated", FabricMountingManagerTestHelper::isTagAllocated),
      makeNativeMethod(
          "synchronouslyUpdateAnimatedProps",
          FabricMountingManagerTestHelper::synchronouslyUpdateAnimatedProps),
      makeNativeMethod(
          "createRuntimeExecutor",
          FabricMountingManagerTestHelper::createRuntimeExecutor),
      makeNativeMethod(
          "createRuntimeScheduler",
          FabricMountingManagerTestHelper::createRuntimeScheduler),
      makeNativeMethod(
          "registerViewComponent",
          FabricMountingManagerTestHelper::registerViewComponent),
      makeNativeMethod(
          "commitViews", FabricMountingManagerTestHelper::commitViews),
      makeNativeMethod(
          "recordPulledDeletes",
          FabricMountingManagerTestHelper::recordPulledDeletes),
      makeNativeMethod(
          "pulledDeleteTags",
          FabricMountingManagerTestHelper::pulledDeleteTags),
  });
}

} // namespace facebook::react
