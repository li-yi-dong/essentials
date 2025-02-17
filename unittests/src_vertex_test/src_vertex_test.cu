#include <gunrock/applications/application.hxx>

using namespace gunrock;
using namespace memory;

void test_get_source_vertex(int num_arguments, char** argument_array) {
  if (num_arguments != 2) {
    std::cerr << "usage: ./bin/<program-name> filename.mtx" << std::endl;
    exit(1);
  }

  // --
  // Define types

  using vertex_t = int;
  using edge_t = int;
  using weight_t = float;

  // --
  // IO

  std::string filename = argument_array[1];

  io::matrix_market_t<vertex_t, edge_t, weight_t> mm;

  using csr_t =
      format::csr_t<memory_space_t::device, vertex_t, edge_t, weight_t>;
  csr_t csr;
  
  csr.from_coo(mm.load(filename));

  // --
  // Build graph

  auto G = graph::build::from_csr<memory_space_t::device, graph::view_t::csr>(
      csr.number_of_rows,               // rows
      csr.number_of_columns,            // columns
      csr.number_of_nonzeros,           // nonzeros
      csr.row_offsets.data().get(),     // row_offsets
      csr.column_indices.data().get(),  // column_indices
      csr.nonzero_values.data().get()   // values
  );  // supports row_indices and column_offsets (default = nullptr)


  auto multi_context = std::shared_ptr<cuda::multi_context_t>(new cuda::multi_context_t(0));

  auto log_edge = [G] __device__(edge_t const& e) -> void {
    auto src = G.get_source_vertex(e);
    auto dst = G.get_destination_vertex(e);
    printf("%d %d %d\n", e, src, dst);
  };

  operators::parallel_for::execute<operators::parallel_for_each_t::edge>(
      G, log_edge, *multi_context);
}

int main(int argc, char** argv) {
  test_get_source_vertex(argc, argv);
}
