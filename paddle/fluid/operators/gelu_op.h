/* Copyright (c) 2020 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#pragma once
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <algorithm>
#include <cmath>
#include "paddle/fluid/framework/eigen.h"
#include "paddle/fluid/framework/op_registry.h"
#include "paddle/fluid/operators/math/blas.h"
#include "paddle/fluid/platform/float16.h"

#ifdef PADDLE_WITH_MKLDNN
#include "paddle/fluid/platform/mkldnn_helper.h"
#endif

namespace paddle {
namespace operators {

template <typename T>
struct GeluFunctor {
  template <typename Device, typename X, typename Out>
  void operator()(Device d, X x, Out out, bool approximate) const {
    if (approximate) {
      // gelu(x) = 0.5 * x * (1 + tanh(sqrt(2 / \pi) * (x + 0.044715 * x^{3})))
      auto temp = (static_cast<T>(M_2_SQRTPI * M_SQRT1_2) *
                   (x + static_cast<T>(0.044715) * x.cube()))
                      .tanh();
      out.device(d) = x * static_cast<T>(0.5) * (static_cast<T>(1) + temp);
    } else {
      // gelu(x) = 0.5 * x *  (1 + erf(x / sqrt(2)))
      auto temp = (x * static_cast<T>(M_SQRT1_2)).erf();
      out.device(d) = x * static_cast<T>(0.5) * (static_cast<T>(1) + temp);
    }
  }
};

template <typename T>
struct GeluGradFunctor {
  template <typename Device, typename X, typename dOut, typename dX>
  void operator()(Device d, X x, dOut dout, dX dx, bool approximate) const {
    if (approximate) {
      const T kAlpha = static_cast<T>(M_2_SQRTPI * M_SQRT1_2);
      const T kBeta = kAlpha * static_cast<T>(0.044715) * static_cast<T>(3);
      const auto y =
          (kAlpha * ((static_cast<T>(0.044715) * x.cube()) + x)).tanh();
      dx.device(d) = static_cast<T>(0.5) * dout *
                     (static_cast<T>(1) + y +
                      (x - x * y.square()) * (kAlpha + kBeta * x.square()));
    } else {
      // gelu_grad(x) = dout * 0.5 * (1 + erf(x / sqrt(2)) + x * sqrt(2 / pi) *
      // exp(- x^2 / 2)
      auto first =
          static_cast<T>(0.5) *
          (static_cast<T>(1) + ((x * static_cast<T>(M_SQRT1_2)).erf()));

      auto second = static_cast<T>(0.5 * M_2_SQRTPI * M_SQRT1_2) * x *
                    (-static_cast<T>(0.5) * x.square()).exp();
      dx.device(d) = dout * (first + second);
    }
  }
};

template <typename DeviceContext, typename T>
class GeluKernel : public framework::OpKernel<T> {
 public:
  void Compute(const framework::ExecutionContext& context) const override {
    auto* out = context.Output<framework::Tensor>("Out");
    auto* in = context.Input<framework::Tensor>("X");
    auto approximate = context.Attr<bool>("approximate");
    out->mutable_data<T>(in->place());

    auto eigen_out = framework::EigenVector<T>::Flatten(*out);
    auto eigen_in = framework::EigenVector<T>::Flatten(*in);
    auto& place =
        *context.template device_context<DeviceContext>().eigen_device();

    GeluFunctor<T> functor;
    functor(place, eigen_in, eigen_out, approximate);
  }
};

template <typename DeviceContext, typename T>
class GeluGradKernel : public framework::OpKernel<T> {
 public:
  void Compute(const framework::ExecutionContext& context) const override {
    auto* x = context.Input<framework::Tensor>("X");
    auto* dout =
        context.Input<framework::Tensor>(framework::GradVarName("Out"));
    auto* dx = context.Output<framework::Tensor>(framework::GradVarName("X"));
    auto approximate = context.Attr<bool>("approximate");
    dx->mutable_data<T>(dout->place());

    auto eigen_x = framework::EigenVector<T>::Flatten(*x);
    auto eigen_dout = framework::EigenVector<T>::Flatten(*dout);
    auto eigen_dx = framework::EigenVector<T>::Flatten(*dx);
    auto& place =
        *context.template device_context<DeviceContext>().eigen_device();

    GeluGradFunctor<T> functor;
    functor(place, eigen_x, eigen_dout, eigen_dx, approximate);
  }
};

}  // namespace operators
}  // namespace paddle
