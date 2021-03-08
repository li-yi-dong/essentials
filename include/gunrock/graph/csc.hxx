#pragma once

#include <cassert>
#include <tuple>
#include <iterator>

#include <gunrock/memory.hxx>
#include <gunrock/util/type_traits.hxx>
#include <gunrock/graph/vertex_pair.hxx>

#include <gunrock/algorithms/search/binary_search.hxx>

namespace gunrock {
namespace graph {

struct empty_csc_t {};

using namespace memory;

template <typename vertex_t, typename edge_t, typename weight_t>
class graph_csc_t {
  using vertex_type = vertex_t;
  using edge_type = edge_t;
  using weight_type = weight_t;

  using vertex_pair_type = vertex_pair_t<vertex_t>;

 public:
  __host__ __device__ graph_csc_t()
      : offsets(nullptr), indices(nullptr), values(nullptr) {}

  // Disable copy ctor and assignment operator.
  // We do not want to let user copy only a slice.
  // Explanation:
  // https://www.geeksforgeeks.org/preventing-object-copy-in-cpp-3-different-ways/
  // graph_csc_t(const graph_csc_t& rhs) = delete;               // Copy
  // constructor graph_csc_t& operator=(const graph_csc_t& rhs) = delete;    //
  // Copy assignment

  // Override pure virtual functions
  // Must use [override] keyword to identify functions that are
  // overriding the derived class
  __host__ __device__ __forceinline__ edge_type
  get_number_of_neighbors(const vertex_type& v) const {
    return (offsets[v + 1] - offsets[v]);
  }

  __host__ __device__ __forceinline__ vertex_type
  get_source_vertex(const edge_type& e) const {
    // XXX: I am dumb, idk if this is upper or lower bound?
    return (vertex_type)algo::search::binary::upper_bound(
        get_column_offsets(), e, this->number_of_vertices);
  }

  __host__ __device__ __forceinline__ vertex_type
  get_destination_vertex(const edge_type& e) const {
    return indices[e];
  }

  __host__ __device__ __forceinline__ edge_type
  get_starting_edge(vertex_type const& v) const {
    return offsets[v];
  }

  __host__ __device__ __forceinline__ vertex_pair_type
  get_source_and_destination_vertices(const edge_type& e) const {
    return {get_source_vertex(e), get_destination_vertex(e)};
  }

  __host__ __device__ __forceinline__ edge_type
  get_edge(const vertex_type& source, const vertex_type& destination) const {
    // XXX: ...
  }

  __host__ __device__ __forceinline__ weight_type
  get_edge_weight(edge_type const& e) const {
    return values[e];
  }

  // Representation specific functions
  // ...
  __host__ __device__ __forceinline__ auto get_column_offsets() const {
    return offsets;
  }

  __host__ __device__ __forceinline__ auto get_row_indices() const {
    return indices;
  }

  __host__ __device__ __forceinline__ auto get_nonzero_values() const {
    return values;
  }

  //  protected:
  __host__ __device__ void set(vertex_type const& _number_of_vertices,
                               edge_type const& _number_of_edges,
                               edge_type* Ap,
                               vertex_type* Aj,
                               weight_type* Ax) {
    this->number_of_vertices = _number_of_vertices;
    this->number_of_edges = _number_of_edges;
    // Set raw pointers
    offsets = raw_pointer_cast<edge_type>(Aj);
    indices = raw_pointer_cast<vertex_type>(Ap);
    values = raw_pointer_cast<weight_type>(Ax);
  }

 private:
  // Underlying data storage
  vertex_type number_of_vertices;  // XXX: redundant
  edge_type number_of_edges;       // XXX: redundant

  edge_type* offsets;
  vertex_type* indices;
  weight_type* values;

};  // struct graph_csc_t

}  // namespace graph
}  // namespace gunrock