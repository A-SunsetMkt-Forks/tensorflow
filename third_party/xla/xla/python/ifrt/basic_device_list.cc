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

#include "xla/python/ifrt/basic_device_list.h"

#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>

#include "absl/base/call_once.h"
#include "absl/base/optimization.h"
#include "absl/hash/hash.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "xla/python/ifrt/device.h"
#include "xla/python/ifrt/device.pb.h"
#include "xla/python/ifrt/device_list.h"
#include "xla/tsl/concurrency/ref_count.h"

namespace xla {
namespace ifrt {
char BasicDeviceList::ID = 0;

DeviceListRef BasicDeviceList::Create(Devices devices) {
  return DeviceListRef(tsl::MakeRef<BasicDeviceList>(std::move(devices)));
}

DeviceListRef BasicDeviceList::Create(absl::Span<Device* const> devices) {
  return Create(Devices(devices.begin(), devices.end()));
}

DeviceListRef BasicDeviceList::Create(std::initializer_list<Device*> devices) {
  return Create(Devices(devices.begin(), devices.end()));
}

BasicDeviceList::BasicDeviceList(Devices devices)
    : devices_(std::move(devices)), hash_(kUnsetHash) {}

DeviceList* BasicDeviceList::AddressableDeviceList() const {
  absl::call_once(addressable_device_list_cache_.once_flag, [this] {
    Devices addressable_devices;
    for (Device* device : devices_) {
      if (device->IsAddressable()) {
        addressable_devices.push_back(device);
      }
    }
    const bool already_fully_addressable =
        addressable_devices.size() == devices_.size();
    if (already_fully_addressable) {
      // `device_list_holder` is intentionally unset. We skip storing a
      // reference-counted copy in the holder to avoid creating a self cycle.
      addressable_device_list_cache_.device_list =
          const_cast<BasicDeviceList*>(this);
    } else {
      addressable_device_list_cache_.device_list_holder =
          BasicDeviceList::Create(std::move(addressable_devices));
      addressable_device_list_cache_.device_list =
          addressable_device_list_cache_.device_list_holder.get();
    }
  });
  return addressable_device_list_cache_.device_list;
}

uint64_t BasicDeviceList::hash() const {
  uint64_t hash = hash_.load(std::memory_order_relaxed);
  if (ABSL_PREDICT_FALSE(hash == kUnsetHash)) {
    hash = absl::HashOf(devices());
    if (ABSL_PREDICT_FALSE(hash == kUnsetHash)) {
      ++hash;
    }
    hash_.store(hash, std::memory_order_relaxed);
  }
  return hash;
}

std::string BasicDeviceList::ToString() const {
  return absl::StrCat("BasicDeviceList([",
                      absl::StrJoin(devices_, ",",
                                    [](std::string* out, Device* device) {
                                      absl::StrAppend(out, device->ToString());
                                    }),
                      "])");
}

}  // namespace ifrt
}  // namespace xla
