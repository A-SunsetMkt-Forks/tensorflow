/* Copyright 2024 The TensorFlow Authors. All Rights Reserved.

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

#include "xla/backends/gpu/codegen/emitters/ir/xla_gpu_ops.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/LogicalResult.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/AffineExpr.h"
#include "mlir/IR/Builders.h"  // IWYU pragma: keep
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"  // IWYU pragma: keep
#include "mlir/IR/MLIRContext.h"  // IWYU pragma: keep
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/PatternMatch.h"  // IWYU pragma: keep
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/TypeRange.h"
#include "mlir/IR/TypeUtilities.h"  // IWYU pragma: keep
#include "mlir/IR/Types.h"
#include "mlir/IR/Value.h"
#include "mlir/IR/ValueRange.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/LogicalResult.h"
#include "xla/backends/gpu/codegen/emitters/ir/xla_gpu_dialect.cc.inc"
#include "xla/hlo/analysis/indexing_map.h"
#include "xla/hlo/analysis/indexing_map_serialization.h"

namespace xla {
namespace gpu {
namespace {

using llvm::ArrayRef;
using mlir::AffineExpr;
using mlir::DenseI64ArrayAttr;
using mlir::failure;
using mlir::Location;
using mlir::LogicalResult;
using mlir::MLIRContext;
using mlir::OpAsmParser;
using mlir::OpAsmPrinter;
using mlir::OperationState;
using mlir::ParseResult;
using mlir::RankedTensorType;
using mlir::SmallVector;
using mlir::success;
using mlir::Type;
using mlir::TypeRange;
using mlir::ValueRange;

}  // namespace

//===----------------------------------------------------------------------===//
// AllocateSharedOp
//===----------------------------------------------------------------------===//

void AllocateSharedOp::getAsmResultNames(
    llvm::function_ref<void(mlir::Value, mlir::StringRef)> setNameFn) {
  setNameFn(getResult(), "shmem");
}

//===----------------------------------------------------------------------===//
// InsertOp
//===----------------------------------------------------------------------===//

LogicalResult InsertOp::verify() {
  if (!getMap().getIndexingMap().GetRangeVars().empty()) {
    return emitOpError() << "insert_op map must not have any symbols";
  }
  int64_t vector_map_num_results =
      getSource().getType().getIndexingMapAttr().getNumResults();
  if (vector_map_num_results != getMap().getIndexingMap().GetDimVars().size()) {
    return emitOpError() << "source map result count must equal insert_op's "
                            "map's dimension count";
  }
  return success();
}

//===----------------------------------------------------------------------===//
// ReduceOp
//===----------------------------------------------------------------------===//

SmallVector<Type, 2> inferReductionResultTypes(TypeRange input_types,
                                               ArrayRef<int64_t> reduced_dims) {
  auto input_shape =
      mlir::cast<RankedTensorType>(input_types.front()).getShape();
  auto num_reduced_dims = reduced_dims.size();
  SmallVector<int64_t, 4> output_shape;
  output_shape.reserve(input_shape.size() - num_reduced_dims);
  int reduce_dim = 0;
  for (int64_t i = 0; i < input_shape.size(); ++i) {
    if (reduce_dim < num_reduced_dims && i == reduced_dims[reduce_dim]) {
      ++reduce_dim;
      continue;
    }
    output_shape.push_back(input_shape[i]);
  }
  SmallVector<Type, 2> result_types;
  result_types.reserve(input_types.size());
  for (auto input_type : input_types) {
    result_types.push_back(RankedTensorType::get(
        output_shape,
        mlir::cast<RankedTensorType>(input_type).getElementType()));
  }
  return result_types;
}

SmallVector<Type, 2> inferReductionInitTypes(TypeRange input_types) {
  SmallVector<Type, 2> init_types;
  init_types.reserve(input_types.size());
  for (auto input_type : input_types) {
    init_types.push_back(
        mlir::cast<RankedTensorType>(input_type).getElementType());
  }
  return init_types;
}

LogicalResult ReduceOp::inferReturnTypes(
    MLIRContext* context, std::optional<Location> location, ValueRange operands,
    mlir::DictionaryAttr attributes, mlir::OpaqueProperties properties,
    mlir::RegionRange regions,
    mlir::SmallVectorImpl<Type>& inferredReturnTypes) {
  ReduceOp::Adaptor adaptor(operands, attributes, properties, regions);
  inferredReturnTypes.append(inferReductionResultTypes(
      TypeRange{adaptor.getInputs()}, adaptor.getDimensions()));
  return success();
}

ParseResult ReduceOp::parse(OpAsmParser& parser, OperationState& result) {
  SmallVector<OpAsmParser::UnresolvedOperand, 4> inputs;
  SmallVector<OpAsmParser::UnresolvedOperand, 4> inits;
  SmallVector<int64_t, 2> dimensions;
  mlir::StringAttr combiner;
  SmallVector<Type, 2> input_types;
  SmallVector<Type, 2> result_types;

  if (parser.parseLParen() || parseOperands(parser, &inputs) ||
      parser.parseRParen() || parser.parseKeyword("inits") ||
      parser.parseLParen() || parseOperands(parser, &inits) ||
      parser.parseRParen() || parser.parseKeyword("dimensions") ||
      parser.parseEqual() ||
      parser.parseCommaSeparatedList(OpAsmParser::Delimiter::Square,
                                     [&]() -> ParseResult {
                                       return parser.parseInteger(
                                           dimensions.emplace_back());
                                     }) ||
      parser.parseKeyword("combiner") || parser.parseEqual() ||
      parser.parseSymbolName(combiner) ||
      parser.parseOptionalAttrDict(result.attributes) ||
      parser.parseColonTypeList(input_types) || parser.parseKeyword("to") ||
      parser.parseTypeList(result_types)) {
    return failure();
  }
  auto ctx = result.getContext();
  mlir::OperationName opname(ReduceOp::getOperationName(), ctx);
  result.addAttribute(ReduceOp::getDimensionsAttrName(opname),
                      DenseI64ArrayAttr::get(ctx, dimensions));
  result.addAttribute(ReduceOp::getCombinerAttrName(opname),
                      mlir::FlatSymbolRefAttr::get(ctx, combiner));
  result.addTypes(result_types);

  auto init_types = inferReductionInitTypes(input_types);
  mlir::SMLoc loc = parser.getCurrentLocation();
  if (parser.resolveOperands(inputs, input_types, loc, result.operands) ||
      parser.resolveOperands(inits, init_types, loc, result.operands)) {
    return failure();
  }
  return success();
}

void ReduceOp::print(OpAsmPrinter& p) {
  p << '(' << getInputs() << ") inits(" << getInits() << ") dimensions=["
    << getDimensions() << "] combiner=@" << getCombiner();
  p.printOptionalAttrDict((*this)->getAttrs(),
                          {getCombinerAttrName(), getDimensionsAttrName()});
  p << " : " << TypeRange(getInputs()) << " to " << TypeRange(getResults());
}

LogicalResult ReduceOp::verify() {
  // Check init types.
  auto inferred_init_types = inferReductionInitTypes(TypeRange(getInputs()));
  for (auto [inferred_init_type, init_type] :
       llvm::zip(inferred_init_types, TypeRange(getInits()))) {
    if (inferred_init_type != init_type) {
      return emitOpError() << "init type " << init_type
                           << " does not match inferred type "
                           << inferred_init_type;
    }
  }
  // Check combiner.
  auto module = this->getOperation()->getParentOfType<mlir::ModuleOp>();
  auto combiner = module.lookupSymbol<mlir::func::FuncOp>(getCombinerAttr());
  if (!combiner) {
    return emitOpError() << "combiner `@" << getCombiner() << "` not found";
  }
  SmallVector<Type, 2> combiner_operand_types;
  combiner_operand_types.reserve(getNumOperands());
  combiner_operand_types.append(inferred_init_types);
  combiner_operand_types.append(inferred_init_types);
  auto expected_combiner_type = mlir::FunctionType::get(
      getContext(), combiner_operand_types, inferred_init_types);
  if (expected_combiner_type != combiner.getFunctionType()) {
    return emitOpError() << "provided combiner `@" << getCombiner()
                         << " expected to have type " << expected_combiner_type
                         << " but got " << combiner.getFunctionType();
  }
  return success();
}

//===----------------------------------------------------------------------===//
// ShuffleReduceOp
//===----------------------------------------------------------------------===//

ParseResult ShuffleReduceOp::parse(OpAsmParser& parser,
                                   OperationState& result) {
  SmallVector<OpAsmParser::UnresolvedOperand, 4> inputs;
  mlir::StringAttr combiner;
  int64_t max_distance;
  SmallVector<Type, 2> operand_types;
  mlir::SMLoc loc = parser.getCurrentLocation();
  if (parser.parseLParen() || parseOperands(parser, &inputs) ||
      parser.parseRParen() || parser.parseKeyword("to") ||
      parser.parseInteger(max_distance) || parser.parseKeyword("combiner") ||
      parser.parseEqual() || parser.parseSymbolName(combiner) ||
      parser.parseOptionalAttrDict(result.attributes) ||
      parser.parseColonTypeList(operand_types) ||
      parser.resolveOperands(inputs, operand_types, loc, result.operands)) {
    return failure();
  }
  auto ctx = result.getContext();
  mlir::OperationName opname(ShuffleReduceOp::getOperationName(), ctx);
  result.addAttribute(ShuffleReduceOp::getCombinerAttrName(opname),
                      mlir::FlatSymbolRefAttr::get(ctx, combiner));
  result.addAttribute(
      ShuffleReduceOp::getMaxDistanceAttrName(opname),
      mlir::IntegerAttr::get(mlir::IntegerType::get(ctx, 64), max_distance));
  result.addTypes(operand_types);
  return success();
}

void ShuffleReduceOp::print(OpAsmPrinter& p) {
  p << '(' << getOperands() << ") to " << getMaxDistance() << " combiner=@"
    << getCombiner();
  p.printOptionalAttrDict((*this)->getAttrs(),
                          {getCombinerAttrName(), getMaxDistanceAttrName()});
  p << " : " << TypeRange(getResultTypes());
}

//===----------------------------------------------------------------------===//
// SyncThreadsOp
//===----------------------------------------------------------------------===//

void SyncThreadsOp::getAsmResultNames(
    llvm::function_ref<void(mlir::Value, mlir::StringRef)> setNameFn) {
  for (auto result : getResults()) {
    setNameFn(result, "synced_tensor");
  }
}

}  // namespace gpu
}  // namespace xla

#define GET_OP_CLASSES
#include "xla/backends/gpu/codegen/emitters/ir/xla_gpu_ops.cc.inc"
