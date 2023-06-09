/* Copyright 2022 The DeepRec Authors. All Rights Reserved.

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

// An optimization pass that groups nodes marked with a common
// kCgmodeClusterAttr into functions, and replaces the original nodes by
// calls. The calls are annotated with kCgmodeCompiledKernelAttr.

#ifndef TENSORFLOW_COMPILER_JIT_ENCAPSULATE_CUDA_GRAPH_MODE_SUBGRAPHS_PASS_H_
#define TENSORFLOW_COMPILER_JIT_ENCAPSULATE_CUDA_GRAPH_MODE_SUBGRAPHS_PASS_H_

#include "tensorflow/core/common_runtime/optimization_registry.h"
#include "tensorflow/core/framework/function.h"
#include "tensorflow/core/graph/graph.h"
#include "tensorflow/core/lib/core/status.h"

namespace tensorflow {

// A rewriting function to apply to each subgraph during encapsulation.
// 'arg_source_tensors' are the tensors corresponding to the arguments in the
// original source graph (*not* 'graph').
//
// 'graph' is the subgraph. The rewriting may renumber the inputs and outputs;
// 'input_permutation' is a mapping from old argument numbers to new argument
// numbers, whereas 'output_permutation' is the same for outputs. Both
// 'input_permutation' and 'output_permutation' are initialized to the identity
// permutation. 'nodedef' is the NodeDef for the call to the function under
// construction, provided to allow additional attributes to be set.
// The rewrite may also change the NodeDef's operator name, and that
// name will be used as the name of the generated function.
typedef std::function<Status(std::unique_ptr<Graph>* graph, NodeDef* node_def)>
    ValidateCgmodeSubgraphFn;

// Transformation that finds subgraphs whose nodes are marked with
// 'group_attribute', splits those subgraphs into functions, and replaces
// the originals with function calls.
//
// 'group_attribute' must be a string valued-attribute that names the new
// functions to introduce.
//
// If 'rewrite_subgraph_fn' is set, it is applied to each subgraph before
// function conversion.
//
// If 'reuse_existing_functions' is set, use an existing function with the
// same name, if any.
//
// TODO(phawkins): currently, some information in control edges
// is not preserved. Suppose you have A and B in the main
// graph, C and D in a subgraph. B and C have control deps from A, D has control
// dep from B. Originally D must run after C, post-transformation this
// dependency is lost.
Status EncapsulateCGModeSubgraphsInFunctions(
    string group_attribute, const Graph& graph_in,
    const ValidateCgmodeSubgraphFn& rewrite_subgraph_fn,
    std::unique_ptr<Graph>* graph_out, FunctionLibraryDefinition* library);

// The attribute that marks function calls produced by the encapsulate
// subgraphs pass and that should in turn be running via graph mode.
extern const char* const kCgmodeCompiledAttr;

// Does `node` have the kCgmodeCompiledAttr attribute?
bool IsCgmodeCompiledKernel(const Node& node);

// Functions produced by the EncapsulateSubgraphs pass have their arguments in
// the order:
// 1) compile-time constant arguments, in host memory,
// 2) other arguments, in device memory.
// 3) resource variable arguments, in host memory. Note that only the resource
//    Tensor itself is in host memory; the underlying value may be in device
//    memory.
// The functions are annotated with the following attributes that describe how
// many constant and resource arguments there are:

// Name of the attribute containing the number of constant arguments.
extern const char* const kCgmodeNumConstantArgsAttr;

// Name of the attribute containing the number of resource variable arguments.
extern const char* const kCgmodeNumResourceArgsAttr;

// Sorts each node's control inputs by their names. This guarantees that for two
// structually equivalent GraphDefs, we get the same traversal ordering on
// node's control input fields.
// TODO(hpucha): Move the utilities to a more appropriate place.
// void SortControlInputs(GraphDef* gdef);

class EncapsulateCGModeSubgraphsPass : public GraphOptimizationPass {
 public:
  Status Run(const GraphOptimizationPassOptions& options) override;
};

}  // namespace tensorflow

#endif  // TENSORFLOW_COMPILER_JIT_ENCAPSULATE_CUDA_GRAPH_MODE_SUBGRAPHS_PASS_H_
