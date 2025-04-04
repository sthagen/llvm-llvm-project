//===-- NativeRegisterContextWindows_arm64.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__aarch64__) || defined(_M_ARM64)
#ifndef liblldb_NativeRegisterContextWindows_arm64_h_
#define liblldb_NativeRegisterContextWindows_arm64_h_

#include "Plugins/Process/Utility/NativeRegisterContextDBReg_arm64.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_arm64.h"
#include "Plugins/Process/Utility/lldb-arm64-register-enums.h"

#include "NativeRegisterContextWindows.h"

namespace lldb_private {

class NativeThreadWindows;

class NativeRegisterContextWindows_arm64
    : public NativeRegisterContextWindows,
      public NativeRegisterContextDBReg_arm64 {
public:
  NativeRegisterContextWindows_arm64(const ArchSpec &target_arch,
                                     NativeThreadProtocol &native_thread);

  uint32_t GetRegisterSetCount() const override;

  const RegisterSet *GetRegisterSet(uint32_t set_index) const override;

  Status ReadRegister(const RegisterInfo *reg_info,
                      RegisterValue &reg_value) override;

  Status WriteRegister(const RegisterInfo *reg_info,
                       const RegisterValue &reg_value) override;

  Status ReadAllRegisterValues(lldb::WritableDataBufferSP &data_sp) override;

  Status WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

protected:
  Status GPRRead(const uint32_t reg, RegisterValue &reg_value);

  Status GPRWrite(const uint32_t reg, const RegisterValue &reg_value);

  Status FPRRead(const uint32_t reg, RegisterValue &reg_value);

  Status FPRWrite(const uint32_t reg, const RegisterValue &reg_value);

private:
  bool IsGPR(uint32_t reg_index) const;

  bool IsFPR(uint32_t reg_index) const;

  llvm::Error ReadHardwareDebugInfo() override;

  llvm::Error WriteHardwareDebugRegs(DREGType hwbType) override;
};

} // namespace lldb_private

#endif // liblldb_NativeRegisterContextWindows_arm64_h_
#endif // defined(__aarch64__) || defined(_M_ARM64)
