/* Copyright 2025 The OpenXLA Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "xla/backends/profiler/gpu/cupti_pm_sampler_stub.h"

#include "absl/status/status.h"

namespace xla {
namespace profiler {

absl::Status CuptiPmSamplerStub::StartSampler() { return absl::OkStatus(); }
absl::Status CuptiPmSamplerStub::StopSampler() { return absl::OkStatus(); }
absl::Status CuptiPmSamplerStub::Deinitialize() { return absl::OkStatus(); }

}  // namespace profiler
}  // namespace xla
