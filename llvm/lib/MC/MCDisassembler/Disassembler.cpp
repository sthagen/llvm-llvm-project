//===-- lib/MC/Disassembler.cpp - Disassembler Public C Interface ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Disassembler.h"
#include "llvm-c/Disassembler.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCDisassembler/MCRelocationInfo.h"
#include "llvm/MC/MCDisassembler/MCSymbolizer.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <cstring>

using namespace llvm;

// LLVMCreateDisasm() creates a disassembler for the TripleName.  Symbolic
// disassembly is supported by passing a block of information in the DisInfo
// parameter and specifying the TagType and callback functions as described in
// the header llvm-c/Disassembler.h .  The pointer to the block and the
// functions can all be passed as NULL.  If successful, this returns a
// disassembler context.  If not, it returns NULL.
//
LLVMDisasmContextRef
LLVMCreateDisasmCPUFeatures(const char *TT, const char *CPU,
                            const char *Features, void *DisInfo, int TagType,
                            LLVMOpInfoCallback GetOpInfo,
                            LLVMSymbolLookupCallback SymbolLookUp) {
  // Get the target.
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(TT, Error);
  if (!TheTarget)
    return nullptr;

  std::unique_ptr<const MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TT));
  if (!MRI)
    return nullptr;

  MCTargetOptions MCOptions;
  // Get the assembler info needed to setup the MCContext.
  std::unique_ptr<const MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, TT, MCOptions));
  if (!MAI)
    return nullptr;

  std::unique_ptr<const MCInstrInfo> MII(TheTarget->createMCInstrInfo());
  if (!MII)
    return nullptr;

  std::unique_ptr<const MCSubtargetInfo> STI(
      TheTarget->createMCSubtargetInfo(TT, CPU, Features));
  if (!STI)
    return nullptr;

  // Set up the MCContext for creating symbols and MCExpr's.
  std::unique_ptr<MCContext> Ctx(
      new MCContext(Triple(TT), MAI.get(), MRI.get(), STI.get()));
  if (!Ctx)
    return nullptr;

  // Set up disassembler.
  std::unique_ptr<MCDisassembler> DisAsm(
      TheTarget->createMCDisassembler(*STI, *Ctx));
  if (!DisAsm)
    return nullptr;

  std::unique_ptr<MCRelocationInfo> RelInfo(
      TheTarget->createMCRelocationInfo(TT, *Ctx));
  if (!RelInfo)
    return nullptr;

  std::unique_ptr<MCSymbolizer> Symbolizer(TheTarget->createMCSymbolizer(
      TT, GetOpInfo, SymbolLookUp, DisInfo, Ctx.get(), std::move(RelInfo)));
  DisAsm->setSymbolizer(std::move(Symbolizer));

  // Set up the instruction printer.
  int AsmPrinterVariant = MAI->getAssemblerDialect();
  std::unique_ptr<MCInstPrinter> IP(TheTarget->createMCInstPrinter(
      Triple(TT), AsmPrinterVariant, *MAI, *MII, *MRI));
  if (!IP)
    return nullptr;

  LLVMDisasmContext *DC = new LLVMDisasmContext(
      TT, DisInfo, TagType, GetOpInfo, SymbolLookUp, TheTarget, std::move(MAI),
      std::move(MRI), std::move(STI), std::move(MII), std::move(Ctx),
      std::move(DisAsm), std::move(IP));
  if (!DC)
    return nullptr;

  DC->setCPU(CPU);
  return DC;
}

LLVMDisasmContextRef
LLVMCreateDisasmCPU(const char *TT, const char *CPU, void *DisInfo, int TagType,
                    LLVMOpInfoCallback GetOpInfo,
                    LLVMSymbolLookupCallback SymbolLookUp) {
  return LLVMCreateDisasmCPUFeatures(TT, CPU, "", DisInfo, TagType, GetOpInfo,
                                     SymbolLookUp);
}

LLVMDisasmContextRef LLVMCreateDisasm(const char *TT, void *DisInfo,
                                      int TagType, LLVMOpInfoCallback GetOpInfo,
                                      LLVMSymbolLookupCallback SymbolLookUp) {
  return LLVMCreateDisasmCPUFeatures(TT, "", "", DisInfo, TagType, GetOpInfo,
                                     SymbolLookUp);
}

//
// LLVMDisasmDispose() disposes of the disassembler specified by the context.
//
void LLVMDisasmDispose(LLVMDisasmContextRef DCR){
  LLVMDisasmContext *DC = static_cast<LLVMDisasmContext *>(DCR);
  delete DC;
}

/// Emits the comments that are stored in \p DC comment stream.
/// Each comment in the comment stream must end with a newline.
static void emitComments(LLVMDisasmContext *DC,
                         formatted_raw_ostream &FormattedOS) {
  // Flush the stream before taking its content.
  StringRef Comments = DC->CommentsToEmit.str();
  // Get the default information for printing a comment.
  const MCAsmInfo *MAI = DC->getAsmInfo();
  StringRef CommentBegin = MAI->getCommentString();
  unsigned CommentColumn = MAI->getCommentColumn();
  bool IsFirst = true;
  while (!Comments.empty()) {
    if (!IsFirst)
      FormattedOS << '\n';
    // Emit a line of comments.
    FormattedOS.PadToColumn(CommentColumn);
    size_t Position = Comments.find('\n');
    FormattedOS << CommentBegin << ' ' << Comments.substr(0, Position);
    // Move after the newline character.
    Comments = Comments.substr(Position+1);
    IsFirst = false;
  }
  FormattedOS.flush();

  // Tell the comment stream that the vector changed underneath it.
  DC->CommentsToEmit.clear();
}

/// Emits latency information in DC->CommentStream for \p Inst, based
/// on the information available in \p DC.
static void emitLatency(LLVMDisasmContext *DC, const MCInst &Inst) {
  const MCSubtargetInfo *STI = DC->getSubtargetInfo();
  const MCInstrInfo *MCII = DC->getInstrInfo();
  const MCSchedModel &SCModel = STI->getSchedModel();
  int Latency = SCModel.computeInstrLatency(*STI, *MCII, Inst);

  // Report only interesting latencies.
  if (Latency < 2)
    return;

  DC->CommentStream << "Latency: " << Latency << '\n';
}

//
// LLVMDisasmInstruction() disassembles a single instruction using the
// disassembler context specified in the parameter DC.  The bytes of the
// instruction are specified in the parameter Bytes, and contains at least
// BytesSize number of bytes.  The instruction is at the address specified by
// the PC parameter.  If a valid instruction can be disassembled its string is
// returned indirectly in OutString which whos size is specified in the
// parameter OutStringSize.  This function returns the number of bytes in the
// instruction or zero if there was no valid instruction.  If this function
// returns zero the caller will have to pick how many bytes they want to step
// over by printing a .byte, .long etc. to continue.
//
size_t LLVMDisasmInstruction(LLVMDisasmContextRef DCR, uint8_t *Bytes,
                             uint64_t BytesSize, uint64_t PC, char *OutString,
                             size_t OutStringSize){
  LLVMDisasmContext *DC = static_cast<LLVMDisasmContext *>(DCR);
  // Wrap the pointer to the Bytes, BytesSize and PC in a MemoryObject.
  ArrayRef<uint8_t> Data(Bytes, BytesSize);

  uint64_t Size;
  MCInst Inst;
  const MCDisassembler *DisAsm = DC->getDisAsm();
  MCInstPrinter *IP = DC->getIP();
  MCDisassembler::DecodeStatus S;
  SmallVector<char, 64> InsnStr;
  raw_svector_ostream Annotations(InsnStr);
  S = DisAsm->getInstruction(Inst, Size, Data, PC, Annotations);
  switch (S) {
  case MCDisassembler::Fail:
  case MCDisassembler::SoftFail:
    // FIXME: Do something different for soft failure modes?
    return 0;

  case MCDisassembler::Success: {
    StringRef AnnotationsStr = Annotations.str();

    SmallVector<char, 64> InsnStr;
    raw_svector_ostream OS(InsnStr);
    formatted_raw_ostream FormattedOS(OS);

    if (DC->getOptions() & LLVMDisassembler_Option_Color) {
      FormattedOS.enable_colors(true);
      IP->setUseColor(true);
    }

    IP->printInst(&Inst, PC, AnnotationsStr, *DC->getSubtargetInfo(),
                  FormattedOS);

    if (DC->getOptions() & LLVMDisassembler_Option_PrintLatency)
      emitLatency(DC, Inst);

    emitComments(DC, FormattedOS);

    assert(OutStringSize != 0 && "Output buffer cannot be zero size");
    size_t OutputSize = std::min(OutStringSize-1, InsnStr.size());
    std::memcpy(OutString, InsnStr.data(), OutputSize);
    OutString[OutputSize] = '\0'; // Terminate string.

    return Size;
  }
  }
  llvm_unreachable("Invalid DecodeStatus!");
}

//
// LLVMSetDisasmOptions() sets the disassembler's options.  It returns 1 if it
// can set all the Options and 0 otherwise.
//
int LLVMSetDisasmOptions(LLVMDisasmContextRef DCR, uint64_t Options){
  if (Options & LLVMDisassembler_Option_UseMarkup){
      LLVMDisasmContext *DC = static_cast<LLVMDisasmContext *>(DCR);
      MCInstPrinter *IP = DC->getIP();
      IP->setUseMarkup(true);
      DC->addOptions(LLVMDisassembler_Option_UseMarkup);
      Options &= ~LLVMDisassembler_Option_UseMarkup;
  }
  if (Options & LLVMDisassembler_Option_PrintImmHex){
      LLVMDisasmContext *DC = static_cast<LLVMDisasmContext *>(DCR);
      MCInstPrinter *IP = DC->getIP();
      IP->setPrintImmHex(true);
      DC->addOptions(LLVMDisassembler_Option_PrintImmHex);
      Options &= ~LLVMDisassembler_Option_PrintImmHex;
  }
  if (Options & LLVMDisassembler_Option_AsmPrinterVariant){
      LLVMDisasmContext *DC = static_cast<LLVMDisasmContext *>(DCR);
      // Try to set up the new instruction printer.
      const MCAsmInfo *MAI = DC->getAsmInfo();
      const MCInstrInfo *MII = DC->getInstrInfo();
      const MCRegisterInfo *MRI = DC->getRegisterInfo();
      int AsmPrinterVariant = MAI->getAssemblerDialect();
      AsmPrinterVariant = AsmPrinterVariant == 0 ? 1 : 0;
      MCInstPrinter *IP = DC->getTarget()->createMCInstPrinter(
          Triple(DC->getTripleName()), AsmPrinterVariant, *MAI, *MII, *MRI);
      if (IP) {
        DC->setIP(IP);
        DC->addOptions(LLVMDisassembler_Option_AsmPrinterVariant);
        Options &= ~LLVMDisassembler_Option_AsmPrinterVariant;
      }
  }
  if (Options & LLVMDisassembler_Option_SetInstrComments) {
    LLVMDisasmContext *DC = static_cast<LLVMDisasmContext *>(DCR);
    MCInstPrinter *IP = DC->getIP();
    IP->setCommentStream(DC->CommentStream);
    DC->addOptions(LLVMDisassembler_Option_SetInstrComments);
    Options &= ~LLVMDisassembler_Option_SetInstrComments;
  }
  if (Options & LLVMDisassembler_Option_PrintLatency) {
    LLVMDisasmContext *DC = static_cast<LLVMDisasmContext *>(DCR);
    DC->addOptions(LLVMDisassembler_Option_PrintLatency);
    Options &= ~LLVMDisassembler_Option_PrintLatency;
  }
  if (Options & LLVMDisassembler_Option_Color) {
    LLVMDisasmContext *DC = static_cast<LLVMDisasmContext *>(DCR);
    DC->addOptions(LLVMDisassembler_Option_Color);
    Options &= ~LLVMDisassembler_Option_Color;
  }
  return (Options == 0);
}
