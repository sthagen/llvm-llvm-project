add_mlir_dialect_library(MLIROpenACCTransforms
  LegalizeDataValues.cpp

  ADDITIONAL_HEADER_DIRS
  ${MLIR_MAIN_INCLUDE_DIR}/mlir/Dialect/OpenACC

  DEPENDS
  MLIROpenACCPassIncGen
  MLIROpenACCOpsIncGen
  MLIROpenACCEnumsIncGen
  MLIROpenACCAttributesIncGen
  MLIROpenACCMPOpsInterfacesIncGen
  MLIROpenACCOpsInterfacesIncGen
  MLIROpenACCTypeInterfacesIncGen

  LINK_LIBS PUBLIC
  MLIROpenACCDialect
  MLIRFuncDialect
  MLIRIR
  MLIRPass
  MLIRSupport
  MLIRTransforms
)
