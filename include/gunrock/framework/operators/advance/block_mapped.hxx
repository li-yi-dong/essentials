/**
 * @file thread_mapped.hxx
 * @author Muhammad Osama (mosama@ucdavis.edu)
 * @brief
 * @version 0.1
 * @date 2020-10-20
 *
 * @copyright Copyright (c) 2020
 *
 */

#pragma once

#include <gunrock/util/math.hxx>
#include <gunrock/cuda/context.hxx>
#include <gunrock/cuda/launch_box.hxx>
#include <gunrock/cuda/global.hxx>

#include <gunrock/framework/operators/configs.hxx>

#include <thrust/transform_scan.h>
#include <thrust/iterator/discard_iterator.h>

#include <cub/block/block_load.cuh>
#include <cub/block/block_scan.cuh>

namespace gunrock {
namespace operators {
namespace advance {
namespace block_mapped {

template <int THREADS_PER_BLOCK,
          int ITEMS_PER_THREAD,
          typename graph_t,
          typename frontier_t,
          typename work_tiles_t,
          typename operator_t>
void __global__ block_mapped_kernel(graph_t const G,
                                    operator_t op,
                                    frontier_t* input,
                                    frontier_t* output,
                                    std::size_t input_size,
                                    work_tiles_t* offsets) {
  using vertex_t = typename graph_t::vertex_type;
  using edge_t = typename graph_t::edge_type;

  // TODO: accept gunrock::frontier_t instead of typename frontier_t::type_t.
  using type_t = frontier_t;

  // Specialize Block Scan for 1D block of THREADS_PER_BLOCK.
  using block_scan_t = cub::BlockScan<edge_t, THREADS_PER_BLOCK>;
  // using block_load_t =
  //     cub::BlockLoad<edge_t, THREADS_PER_BLOCK, ITEMS_PER_THREAD>;

  auto global_idx = cuda::thread::global::id::x();
  auto local_idx = cuda::thread::local::id::x();

  __shared__ union TempStorage {
    typename block_scan_t::TempStorage scan;
    // typename block_load_t::TempStorage load;
  } storage;

  // Prepare data to process (to shmem/registers).
  __shared__ vertex_t offset[1];
  __shared__ vertex_t vertices[THREADS_PER_BLOCK];
  __shared__ edge_t degrees[THREADS_PER_BLOCK];
  __shared__ edge_t sedges[THREADS_PER_BLOCK];
  edge_t th_deg[ITEMS_PER_THREAD];

  if (global_idx < input_size) {
    vertex_t v = input[global_idx];
    vertices[local_idx] = v;
    if (gunrock::util::limits::is_valid(v)) {
      sedges[local_idx] = G.get_starting_edge(v);
      th_deg[0] = G.get_number_of_neighbors(v);
    } else {
      th_deg[0] = 0;
    }
  } else {
    vertices[local_idx] = gunrock::numeric_limits<vertex_t>::invalid();
    th_deg[0] = 0;
  }
  __syncthreads();

  // Exclusive sum of degrees.
  vertex_t aggregate_degree_per_block;
  block_scan_t(storage.scan)
      .ExclusiveSum(th_deg, th_deg, aggregate_degree_per_block);
  __syncthreads();

  // Store back to shared memory.
  degrees[local_idx] = th_deg[0];

  // Accumulate the output size to global memory, only done once per block.
  if (local_idx == 0)
    if ((cuda::block::id::x() * cuda::block::size::x()) < input_size)
      offset[0] = offsets[cuda::block::id::x() * cuda::block::size::x()];

  __syncthreads();

  // To search for which vertex id we are computing on.
  auto length = global_idx - local_idx + cuda::block::size::x();

  // Bound check.
  if (input_size < length)
    length = input_size;

  length -= global_idx - local_idx;

  for (int i = local_idx;               // threadIdx.x
       i < aggregate_degree_per_block;  // total degree to process
       i += cuda::block::size::x()      // increment by blockDim.x
  ) {
    // Binary search to find which vertex id to work on.
    int id = algo::search::binary::rightmost(degrees, i, length);

    // If the id is greater than the width of the block or the input size, we
    // exit.
    if (id >= length)
      continue;

    // Fetch the vertex corresponding to the id.
    vertex_t v = vertices[id];
    if (!gunrock::util::limits::is_valid(v))
      continue;

    // If the vertex is valid, get its edge, neighbor and edge weight.
    auto e = sedges[id] + i - degrees[id];
    auto n = G.get_destination_vertex(e);
    auto w = G.get_edge_weight(e);

    // Use-defined advance condition.
    bool cond = op(v, n, e, w);

    // Store [neighbor] into the output frontier.
    output[offset[0] + i] =
        (cond && n != v) ? n : gunrock::numeric_limits<vertex_t>::invalid();
  }
}

template <advance_type_t type,
          advance_direction_t direction,
          typename graph_t,
          typename operator_t,
          typename frontier_t,
          typename work_tiles_t>
void execute(graph_t& G,
             operator_t op,
             frontier_t* input,
             frontier_t* output,
             work_tiles_t& segments,
             cuda::standard_context_t& context) {
  // This shouldn't be required for block-mapped.
  auto size_of_output = compute_output_length(G, input, segments, context);

  // If output frontier is empty, resize and return.
  if (size_of_output <= 0) {
    output->set_number_of_elements(0);
    return;
  }

  // <todo> Resize the output (inactive) buffer to the new size.
  // Can be hidden within the frontier struct.
  if (output->get_capacity() < size_of_output)
    output->reserve(size_of_output);
  output->set_number_of_elements(size_of_output);

  // TODO: Use launch_box_t instead.
  constexpr int block_size = 128;
  int grid_size =
      (input->get_number_of_elements() + block_size - 1) / block_size;

  // Launch blocked-mapped advance kernel.
  block_mapped_kernel<block_size, 1>
      <<<grid_size, block_size, 0, context.stream()>>>(
          G, op, input->data(), output->data(), input->get_number_of_elements(),
          segments.data().get());
  context.synchronize();
}

}  // namespace block_mapped
}  // namespace advance
}  // namespace operators
}  // namespace gunrock