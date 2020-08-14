#include <ATen/ATen.h>
#include <ATen/native/sparse/SparseTensorMath.h>
#include <ATen/hammerblade/HammerBladeContext.h>
#include <ATen/native/hammerblade/Offload.h>

namespace at { namespace native {

using namespace at::sparse;

// computes (b@c.T), sampled by self
Tensor sddtmm_hb(const SparseTensor& sample, const Tensor& b, const Tensor& c) {

  TORCH_CHECK(sample.is_hammerblade(), "SddTmm: expected 'sample' to be a HammerBlade tensor");
  TORCH_CHECK(b.is_hammerblade(), "SddTmm: expected 'b' to be a HammerBlade tensor");
  TORCH_CHECK(c.is_hammerblade(), "SddTmm: expected 'c' to be a HammerBlade tensor");

  if ( b.scalar_type() != ScalarType::Float
    || c.scalar_type() != ScalarType::Float ) {
    AT_ERROR("HammerBlade SddTmm is implemented for Float type only for matrices b and c"); 
  }
   
  TORCH_CHECK(sample.sparse_dim() == 2, "We do not support hybrid sparse tensor for 'sample' in HammerBlade SddTmm!");
  TORCH_CHECK(b.dim() == 2 && c.dim() == 2, "Expected 2D matrixes for 'b' and 'c', but got ", b.dim(), " and ", c.dim(), " tensors");
  TORCH_CHECK(b.size(1) == c.size(1), "Matrix multiply dimension mismatch: 'b' dim 1 = ", b.size(1), ", 'c'.T dim 0 = ", c.size(1));
  
  IntTensor indices = sample._indices();
  TORCH_CHECK(indices.dtype() == at::kInt, "Indices on HammerBlade should be int32, but got ", indices.dtype());
  IntTensor colIndices = indices.select(0, 1);
  TORCH_CHECK(colIndices.is_hammerblade(), "colIndices must be HammerBlade Tensor");
  IntTensor rowIndices = indices.select(0, 0);
  TORCH_CHECK(rowIndices.is_hammerblade(), "rowIndices must be HammerBlade Tensor");
  TORCH_CHECK(b.size(0) == sample.size(1) && c.size(0) == sample.size(0),"SddTmm sample dimension mismatch: sample.T was shape ",sample.size(0)," by ",sample.size(1),", but (b@c.T).T is shape ",c.size(0)," by ",b.size(0));

  Tensor result = at::zeros({b.size(0), c.size(0)}, {at::requires_grad().device(at::kHAMMERBLADE).dtype(at::kFloat)});

  hb_offload_kernel(result, colIndices, rowIndices, b, c, "tensorlib_sddtmm");
  return result;
}
   
}}
