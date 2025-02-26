/* Copyright 2015 The TensorFlow Authors. All Rights Reserved.

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

#ifndef TENSORFLOW_CORE_COMMON_RUNTIME_FUNCTION_UTILS_H_
#define TENSORFLOW_CORE_COMMON_RUNTIME_FUNCTION_UTILS_H_

#include <functional>
#include <memory>

#include "tensorflow/core/framework/function.h"
#include "tensorflow/core/lib/core/status.h"

namespace tensorflow {

class AttrSlice;
class Graph;
class GraphDef;
class NameAttrList;
class Node;
class NodeDef;
class OpDef;

// Debugging facility.  Returns a debug string for a graph
// representing an instantiated function.
string DebugString(const Graph* g);

// Dump the contents of the "graph" to log files if the logging level is
// sufficiently high.
void DumpGraph(absl::string_view label, const Graph* g);

// Convert the Graph of a function to a GraphDef.
//
// Handles renaming of nodes to avoid duplicate names which may
// be present after various rewriting operations.
void ToGraphDef(const Graph* g, GraphDef* gdef, bool pretty = false);

// Extracts function name and attributes from `call_def`
// `call_def` can be a native function call (where the op type is the function
// name) or a call through PartitionedCall/StatefulPartitionedCall.
absl::Status NameAndAttrsFromFunctionCall(const NodeDef& call_def,
                                          NameAttrList* function);

// A few hand-crafted optimization on the instantiated function body
// (a Graph*).

// Removes nodes that are
//   1. not stateful; and
//   2. not _Arg; and
//   3. not reachable from _Retval.
//
// This function is triggered by function inlining, unlike 'PruneFunctionBody'
// it doesn't preserve nodes that are reachable from control returns. Function
// inlining is responsible for connecting control return nodes with the nodes
// that have input control edges from the inlined function call node.
//
// Assuming that automatic control dependency tracking is correct, absence of
// outgoing control edge from the function call node means that no one needs to
// observe side-effect that might have been generated by the function (see
// documentation in common_runtime/function.cc for details).
//
// Returns true iff any node is removed from "g".
bool RemoveDeadNodes(Graph* g);

// Find a pattern:
//   src -(in)-> node -(out)-> dst, where
// 1) node is an identity node;
// 2) in is the only incoming data edge;
// 3) out is the only outgoing data edge;
//
// Rewrites the above pattern with src->dst and relevant data
// dependencies updated. Repeat the process until no such pattern
// left.
bool RemoveIdentityNodes(Graph* g);

// Rewrites _ListToArray and _ArrayToList to a set of Identity nodes.
bool RemoveListArrayConverter(Graph* g);

// Extracts function name and attributes from `call_def` and invokes
// flr->Instantiate(name, attrs, handle).
// `call_def` can be a native function call (where the op type is the function
// name) or a call through PartitionedCall/StatefulPartitionedCall.
absl::Status InstantiateFunctionCall(const NodeDef& call_def,
                                     FunctionLibraryRuntime* flr,
                                     FunctionLibraryRuntime::Handle* handle);

// Returns true iff `n` represents a function call. `n` can be a native
// function call (n.type_string() is the function name),
// a PartitionedCall/StatefulPartitionedCall, or a SymbolicGradient (which
// has been deprecated for a while).
bool IsFunctionCall(const FunctionLibraryDefinition& lib_def, const Node& n);
}  // end namespace tensorflow

#endif  // TENSORFLOW_CORE_COMMON_RUNTIME_FUNCTION_UTILS_H_
