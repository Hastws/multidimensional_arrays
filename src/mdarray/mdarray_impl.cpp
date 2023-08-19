#include "mdarray/mdarray_impl.h"

#include <iostream>
#include <utility>

#include "memory_pool/allocator.h"
#include "utils/exception.h"

namespace KD {

MdarrayImpl::MdarrayImpl(const Storage &storage, const Shape &shape,
                         const IndexArray &stride, bool requires_grad)
    : storage_(storage),
      shape_(shape),
      stride_(stride),
      requires_grad_(requires_grad),
      grad_meta_ptr_(nullptr) {
  if (requires_grad_) {
    grad_meta_ptr_ = Allocator::UniqueConstruct<AutoGradMeta>(shape_);
  }
}

MdarrayImpl::MdarrayImpl(Storage storage, Shape shape, bool requires_grad)
    : storage_(std::move(storage)),
      shape_(std::move(shape)),
      stride_(shape_.DimensionsSize()),
      requires_grad_(requires_grad),
      grad_meta_ptr_(nullptr) {
  // if shape_[i] == 1, set stride_[i] = 0. For broadcasting operation.
  for (KD::Index i = 0; i < stride_.ArraySize(); ++i) {
    stride_[i] = shape_[i] == 1 ? 0 : shape_.SubSpaceSize(i + 1);
  }
  if (requires_grad_) {
    grad_meta_ptr_ = Allocator::UniqueConstruct<AutoGradMeta>(shape_);
  }
}

MdarrayImpl::MdarrayImpl(const Shape &shape, bool requires_grad)
    : MdarrayImpl(Storage(shape.SpaceSize()), shape, requires_grad) {}

MdarrayImpl::MdarrayImpl(const BasicData *data, const Shape &shape,
                         bool requires_grad)
    : MdarrayImpl(Storage(data, shape.SpaceSize()), shape, requires_grad) {}

MdarrayImpl::MdarrayImpl(Storage &&storage, Shape &&shape, IndexArray &&stride,
                         bool requires_grad)
    : storage_(std::move(storage)),
      shape_(std::move(shape)),
      stride_(std::move(stride)),
      requires_grad_(requires_grad),
      grad_meta_ptr_(nullptr) {
  if (requires_grad_) {
    grad_meta_ptr_ = Allocator::UniqueConstruct<AutoGradMeta>(shape_);
  }
}

bool MdarrayImpl::IsContiguous() const {
  for (Index i = 0; i < stride_.ArraySize(); i++) {
    if (stride_[i] != 0 && stride_[i] != shape_.SubSpaceSize(i + 1)) {
      return false;
    }
  }
  return true;
}

BasicData &MdarrayImpl::operator[](std::initializer_list<Index> indexes) {
  CHECK_EQUAL(DimensionsSize(), indexes.size(),
              "Invalid " << indexes.size() << " indices for "
                         << DimensionsSize() << " multidimensional_arrays");

  Index offset = 0, i = 0;
  for (auto idx : indexes) {
    CHECK_IN_RANGE(idx, 0, Size(i),
                   "Index " << idx << " is out of bound for dimension " << i
                            << " with Size " << Size(i));
    offset += idx * stride_[i++];
  }
  storage_.IncrementVersion();
  return storage_[offset];
}

BasicData MdarrayImpl::operator[](std::initializer_list<Index> indexes) const {
  CHECK_EQUAL(DimensionsSize(), indexes.size(),
              "Invalid " << indexes.size() << " indices for "
                         << DimensionsSize() << " multidimensional_arrays");

  Index offset = 0, i = 0;
  for (auto idx : indexes) {
    CHECK_IN_RANGE(idx, 0, Size(i),
                   "Index " << idx << " is out of bound for dimension " << i
                            << " with Size " << Size(i));
    offset += idx * stride_[i++];
  }
  return storage_[offset];
}

BasicData MdarrayImpl::Item() const {
  CHECK_TRUE(
      DimensionsSize() == 1 && Size(0) == 1,
      "Only one element multidimensional_arrays can be converted to scalars");
  return storage_[0];
}

Allocator::UniquePtr<MdarrayImpl> MdarrayImpl::Slice(Index dim,
                                                     Index idx) const {
  CHECK_IN_RANGE(dim, 0, DimensionsSize(),
                 "Dimension out of range (expected to be in range of [0, "
                     << DimensionsSize() << "), but got " << dim << ")");
  CHECK_IN_RANGE(idx, 0, Size(dim),
                 "Index " << idx << " is out of bound for dimension " << dim
                          << " with Size " << Size(dim));

  // new_data_ptr = data_ptr + idx * stride_[dim]
  Index offset = stride_[dim] * idx;
  Storage storage(storage_, stride_[dim] * idx);
  // new_shape is the same as shape_ except missing the Size on #dim dimension,
  // and new_stride is similar.
  Shape shape(shape_, dim);
  IndexArray stride(shape_.DimensionsSize() - 1);

  KD::Index i = 0;
  for (; i < dim; i++) {
    stride[i] = stride_[i];
  }
  for (; i < stride.ArraySize(); i++) {
    stride[i] = stride_[i + 1];
  }

  auto ret_ptr = Allocator::UniqueConstruct<MdarrayImpl>(
      std::move(storage), std::move(shape), std::move(stride), false);
  if (requires_grad_) {
    ret_ptr->requires_grad_ = true;
    ret_ptr->grad_meta_ptr_ =
        Allocator::UniqueConstruct<AutoGradMeta>(grad_meta_ptr_->grad_, offset);
    ret_ptr->grad_meta_ptr_->set_from_view(true);
    ret_ptr->grad_meta_ptr_->set_grad_fn(*this);
  }
  return ret_ptr;
}

Allocator::UniquePtr<MdarrayImpl> MdarrayImpl::Slice(Index dim, Index start_idx,
                                                     Index end_idx) const {
  CHECK_IN_RANGE(dim, 0, DimensionsSize(),
                 "Dimension out of range (expected to be in range of [0, "
                     << DimensionsSize() << "), but got " << dim << ")");
  CHECK_IN_RANGE(start_idx, 0, Size(dim),
                 "Index " << start_idx << " is out of bound for dimension "
                          << dim << " with Size " << Size(dim));
  CHECK_IN_RANGE(end_idx, 0, Size(dim) + 1,
                 "Range end " << end_idx << " is out of bound for dimension "
                              << dim << " with Size " << Size(dim));

  // new_data_ptr = data_ptr + start_idx * stride_[dim]
  Index offset = stride_[dim] * start_idx;
  Storage storage(storage_, offset);
  // new_stride is the same as GetStride
  IndexArray stride(stride_);
  // new_shape and shape_ are only different on #dim dimension
  Shape shape(shape_);
  shape[dim] = end_idx - start_idx;

  auto ret_ptr = Allocator::UniqueConstruct<MdarrayImpl>(
      std::move(storage), std::move(shape), std::move(stride), false);
  if (requires_grad_) {
    ret_ptr->requires_grad_ = true;
    ret_ptr->grad_meta_ptr_ =
        Allocator::UniqueConstruct<AutoGradMeta>(grad_meta_ptr_->grad_, offset);
    ret_ptr->grad_meta_ptr_->set_from_view(true);
    ret_ptr->grad_meta_ptr_->set_grad_fn(*this);
  }
  return ret_ptr;
}

Allocator::UniquePtr<MdarrayImpl> MdarrayImpl::Transpose(Index dim1,
                                                         Index dim2) const {
  CHECK_IN_RANGE(dim1, 0, DimensionsSize(),
                 "Dimension out of range (expected to be in range of [0, "
                     << DimensionsSize() << "), but got " << dim1 << ")");
  CHECK_IN_RANGE(dim2, 0, DimensionsSize(),
                 "Dimension out of range (expected to be in range of [0, "
                     << DimensionsSize() << "), but got " << dim2 << ")");

  // new_data_ptr = data_ptr
  // Exchange the value in shape_ and stride_ on #dim1 and #dim2
  Shape shape(shape_);
  shape[dim1] = shape_[dim2];
  shape[dim2] = shape_[dim1];

  IndexArray stride(stride_);
  stride[dim1] = stride_[dim2];
  stride[dim2] = stride_[dim1];

  auto ret_ptr = Allocator::UniqueConstruct<MdarrayImpl>(
      Storage(storage_), std::move(shape), std::move(stride), false);
  if (requires_grad_) {
    ret_ptr->requires_grad_ = true;
    ret_ptr->grad_meta_ptr_ =
        Allocator::UniqueConstruct<AutoGradMeta>(grad_meta_ptr_->grad_, 0);
    ret_ptr->grad_meta_ptr_->set_from_view(true);
    ret_ptr->grad_meta_ptr_->set_grad_fn(*this);
  }
  return ret_ptr;
}

Allocator::UniquePtr<MdarrayImpl> MdarrayImpl::Permute(
    std::initializer_list<Index> dims) const {
  CHECK_EQUAL(dims.size(), DimensionsSize(),
              "Dimension not match (expected dims of "
                  << DimensionsSize() << ", but got " << dims.size() << ")");

  IndexArray shape(DimensionsSize());
  IndexArray stride(DimensionsSize());
  Index i = 0;
  for (Index idx : dims) {
    shape[i] = shape_[idx];
    stride[i] = stride_[idx];
    ++i;
  }
  auto ret_ptr = Allocator::UniqueConstruct<MdarrayImpl>(
      Storage(storage_), std::move(shape), std::move(stride), false);
  if (requires_grad_) {
    ret_ptr->requires_grad_ = true;
    ret_ptr->grad_meta_ptr_ =
        Allocator::UniqueConstruct<AutoGradMeta>(grad_meta_ptr_->grad_, 0);
    ret_ptr->grad_meta_ptr_->set_from_view(true);
    ret_ptr->grad_meta_ptr_->set_grad_fn(*this);
  }
  return ret_ptr;
}

Allocator::UniquePtr<MdarrayImpl> MdarrayImpl::View(const Shape &shape) const {
  CHECK_TRUE(IsContiguous(),
             "View() is only supported to contiguous multidimensional_arrays");
  CHECK_EQUAL(shape.SpaceSize(), shape_.SpaceSize(),
              "Shape of Size "
                  << shape.SpaceSize()
                  << " is invalid for input multidimensional_arrays with Size "
                  << shape_.SpaceSize());
  // new_data_ptr = data_ptr
  // Just use new shape and adjust GetStride.
  auto ret_ptr =
      Allocator::UniqueConstruct<MdarrayImpl>(storage_, shape, false);
  if (requires_grad_) {
    ret_ptr->requires_grad_ = true;
    ret_ptr->grad_meta_ptr_ =
        Allocator::UniqueConstruct<AutoGradMeta>(grad_meta_ptr_->grad_, 0);
    ret_ptr->grad_meta_ptr_->set_from_view(true);
    ret_ptr->grad_meta_ptr_->set_grad_fn(*this);
  }
  return ret_ptr;
}

Allocator::UniquePtr<MdarrayImpl> MdarrayImpl::Squeeze() const {
  Index count = 0;
  auto squeeze_dims_ptr =
      Allocator::UniqueAllocate<Index>(DimensionsSize() * sizeof(Index));
  auto squeeze_dims = squeeze_dims_ptr.get();

  for (Index i = 0; i < shape_.DimensionsSize(); i++)
    if (shape_[i] != 1) squeeze_dims[count++] = shape_[i];
  Shape squeeze_shape(squeeze_dims, count);
  return View(squeeze_shape);
}

Allocator::UniquePtr<MdarrayImpl> MdarrayImpl::Unsqueeze(Index dim) const {
  Index new_dim_size = DimensionsSize() + 1;
  CHECK_IN_RANGE(dim, 0, new_dim_size,
                 "Dimension out of range (expected to be in range of [0, "
                     << new_dim_size << "), but got " << dim << ")");

  auto unsqueeze_dims_ptr =
      Allocator::UniqueAllocate<Index>(new_dim_size * sizeof(Index));
  auto unsqueeze_dims = unsqueeze_dims_ptr.get();

  Index i = 0;
  for (; i != dim; i++) {
    unsqueeze_dims[i] = shape_[i];
  }
  unsqueeze_dims[dim] = 1;
  for (i++; i < new_dim_size; i++) {
    unsqueeze_dims[i] = shape_[i - 1];
  }
  return View(Shape(unsqueeze_dims, new_dim_size));
}

Allocator::UniquePtr<MdarrayImpl> MdarrayImpl::Grad() const {
  CHECK_TRUE(requires_grad_, "The multidimensional_arrays don't require Grad.");
  return Allocator::UniqueConstruct<MdarrayImpl>(grad_meta_ptr_->grad_, shape_,
                                                 stride_, false);
}

BasicData MdarrayImpl::Eval(IndexArray &indexes) const {
  Index offset = 0;
  for (KD::Index i = 0; i < DimensionsSize(); ++i) {
    offset += indexes[i] * stride_[i];
  }
  return storage_[offset];
}

BasicData MdarrayImpl::Eval(Index idx) const { return storage_[idx]; }

}  // namespace KD