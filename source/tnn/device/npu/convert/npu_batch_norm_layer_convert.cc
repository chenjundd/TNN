// Tencent is pleased to support the open source community by making TNN available.
//
// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include <tnn/utils/data_type_utils.h>
#include "graph/attr_value.h"
#include "graph/op/nn_defs.h"
#include "npu_base_layer_convert.h"
#include "npu_utils.h"

namespace TNN_NS {
DECLARE_NPU_LAYER_WEIGHT_ARRAY(BatchNorm, LAYER_BATCH_NORM);

Status NpuBatchNormLayer::Convert() {
    auto resource = dynamic_cast<BatchNormLayerResource *>(resource_);

    if (!resource) {
        LOGE("Error: BatchNorm layer resource is nil\n");
        return Status(TNNERR_MODEL_ERR, "Error: BatchNorm layer resource is nil");
    }

    // channel is the 1 element of NCHW
    int channel             = input_ops_[0]->GetShape()[1];
    RawBuffer &scale_handle = resource->scale_handle;
    bool share_channel      = scale_handle.GetBytesSize() == DataTypeUtils::GetBytesSize(scale_handle.GetDataType());
    auto *scale_data        = resource->scale_handle.force_to<float *>();
    auto *bias_data         = resource->bias_handle.force_to<float *>();

    auto mean_data        = std::make_shared<std::vector<float>>();
    auto variance_data    = std::make_shared<std::vector<float>>();
    auto share_scale_data = std::make_shared<std::vector<float>>();
    auto share_bias_data  = std::make_shared<std::vector<float>>();
    arrays.push_back(mean_data);
    arrays.push_back(variance_data);
    arrays.push_back(share_scale_data);
    arrays.push_back(share_bias_data);

    for (int i = 0; i < channel; i++) {
        mean_data->push_back(0);
        variance_data->push_back(1);
        if (share_channel) {
            share_scale_data->push_back(scale_data[0]);
            share_bias_data->push_back(bias_data[0]);
        }
    }

    ge::Shape shape({channel});
    ge::TensorDesc desc(shape, ge::FORMAT_NCHW, ge::DT_FLOAT);

    auto mean_const = std::make_shared<ge::op::Const>(layer_name_ + "_mean");
    NpuUtils::CreateAttrArray(mean_const, *mean_data, desc, channel);

    auto variance_const = std::make_shared<ge::op::Const>(layer_name_ + "_variance");
    NpuUtils::CreateAttrArray(variance_const, *variance_data, desc, channel);

    auto scale_const = std::make_shared<ge::op::Const>(layer_name_ + "_scale");
    auto bias_const  = std::make_shared<ge::op::Const>(layer_name_ + "_bias");

    if (share_channel) {
        NpuUtils::CreateAttrArray(scale_const, *share_scale_data, desc, channel);
        NpuUtils::CreateAttrArray(bias_const, *share_bias_data, desc, channel);
    } else {
        NpuUtils::CreateAttrValue(scale_const, shape, resource->scale_handle);
        NpuUtils::CreateAttrValue(bias_const, shape, resource->bias_handle);
    }

    weight_ops_.push_back(mean_const);
    weight_ops_.push_back(variance_const);
    weight_ops_.push_back(scale_const);
    weight_ops_.push_back(bias_const);

    auto output = std::make_shared<ge::op::BatchNorm>(outputs_name_[0]);
    output->set_input_x(*input_ops_[0]->GetOperator());
    output->set_input_variance(*variance_const);
    output->set_input_mean(*mean_const);
    output->set_input_scale(*scale_const);
    output->set_input_b(*bias_const);

    std::shared_ptr<OperatorInfo> output_op = std::make_shared<OperatorInfo>(output);
    output_ops_.push_back(output_op);
    return SetOutputOps();
}

REGISTER_NPU_LAYER(BatchNorm, LAYER_BATCH_NORM);

}  // namespace TNN_NS