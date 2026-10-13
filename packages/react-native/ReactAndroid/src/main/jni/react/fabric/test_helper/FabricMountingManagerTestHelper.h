/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbjni/fbjni.h>
#include <react/jni/JRuntimeExecutor.h>
#include <react/jni/JRuntimeScheduler.h>
#include <react/jni/NativeMap.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>

#include <react/fabric/ComponentFactory.h>
#include <react/fabric/FabricMountingManager.h>

namespace facebook::react {

/**
 * JNI test helper that wraps a real FabricMountingManager
 */
class FabricMountingManagerTestHelper : public jni::HybridClass<FabricMountingManagerTestHelper> {
 public:
  static constexpr auto kJavaDescriptor = "Lcom/facebook/react/fabric/FabricMountingManagerTestHelper;";

  void startSurface(jint surfaceId);
  void stopSurface(jint surfaceId);
  void preallocateView(jint surfaceId, jint tag);
  void destroyUnmountedView(jint surfaceId, jint tag);
  bool isTagAllocated(jint surfaceId, jint tag);
  void synchronouslyUpdateAnimatedProps(
      jni::alias_ref<jni::JArrayInt> tags,
      jni::alias_ref<jni::JArrayClass<NativeMap::javaobject>> props);

  // A FabricUIManagerBinding registered on the helper's FabricUIManager with
  // these never runs JavaScript: its surfaces mount only what the helper
  // commits.
  jni::local_ref<JRuntimeExecutor::jhybridobject> createRuntimeExecutor();
  jni::local_ref<JRuntimeScheduler::jhybridobject> createRuntimeScheduler();
  void registerViewComponent(ComponentFactory *componentFactory);

  void commitViews(jint surfaceId, jni::alias_ref<jni::JArrayInt> tags);
  void recordPulledDeletes(jint surfaceId);
  jni::local_ref<jni::JArrayInt> pulledDeleteTags();

  static void registerNatives();

 private:
  friend HybridBase;

  explicit FabricMountingManagerTestHelper(jni::alias_ref<JFabricUIManager::javaobject> jFabricUIManager);

  static void initHybrid(
      jni::alias_ref<jhybridobject> jobj,
      jni::alias_ref<JFabricUIManager::javaobject> jFabricUIManager);

  jni::global_ref<JFabricUIManager::javaobject> javaUIManager_;
  std::shared_ptr<FabricMountingManager> mountingManager_;
  std::unique_ptr<ViewComponentDescriptor> viewComponentDescriptor_;
  std::shared_ptr<RuntimeScheduler> runtimeScheduler_;
  class DeleteRecorder;
  std::shared_ptr<DeleteRecorder> deleteRecorder_;
};

} // namespace facebook::react
