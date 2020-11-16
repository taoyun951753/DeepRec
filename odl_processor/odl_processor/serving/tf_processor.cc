#include "tf_processor.h"
#include "run_predict.h"
#include "saved_model_loader.h"
#include "odl_processor/serving/tf_predict.pb.h"
#include "json.hpp"

#include <stdio.h>
#include <string.h>
#include <iostream>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

extern "C" {
void *initialize(const char *model_entry, const char *model_config,
                 int *state) {
  // Parse model_config for getting inter_op_parallelism_threads
  // and intra_op_parallelism_threads
  int inter_threads = 0, intra_threads = 0, schedule_threads = 0;
  bool warm_up = false;

  try {
    char *schedule_threads_str = getenv("SCHEDULABLE_CPUS");
    if (schedule_threads_str != NULL) {
      schedule_threads = atoi(schedule_threads_str);
    }
    if (strlen(model_config) != 0) {
      auto config = json::parse(model_config);
      if (config.count("inter_op_parallelism_threads") > 0) {
        inter_threads = config["inter_op_parallelism_threads"];
      }
      if (config.count("intra_op_parallelism_threads") > 0) {
        intra_threads = config["intra_op_parallelism_threads"];
      }
      if (config.count("enable_warm_up") > 0) {
        warm_up = config["enable_warm_up"];
      }
    }
    if (inter_threads <= 0) {
      inter_threads = schedule_threads / 2;
    }
    if (intra_threads <= 0) {
      intra_threads = schedule_threads / 2;
    }
  } catch (std::exception e) {
    std::cout << "Parse model_config error: " << e.what() << std::endl;
    *state = -1;
    return NULL;
  }

  SavedModelLoader *loader = new SavedModelLoader(inter_threads, intra_threads);
  *state = loader->LoadModel(model_entry);
  if (warm_up) {
    std::cout << "Load model successfully, start to warm up!" << std::endl;
    try {
      RunRequest request;
      RunResponse response;
      std::string model_signature_info = loader->GetModelSignatureInfo();
      auto signature = json::parse(model_signature_info.c_str());
      if (signature.count("signature_name") > 0) {
        request.SetSignatureName(signature["signature_name"]);
      }
      if (signature.count("inputs") > 0) {
        auto inputs = signature["inputs"];
        for (json::iterator it = inputs.begin(); it != inputs.end(); it++) {
          std::vector<long long> shape;
          auto input_shape = (*it)["shape"];
          int content_size = 1;
          for (json::iterator iter = input_shape.begin(); iter != input_shape.end(); iter++) {
            if ((*iter) > 0) {
              shape.push_back(*iter);
              content_size *= (int)(*iter);
            } else {
              shape.push_back(1);
            }
          }
          if ((*it)["type"] == "DT_FLOAT") {
            std::vector<float> content;
            content.resize(content_size);
            request.AddFeed((*it)["name"], shape, content);
          } else if ((*it)["type"] == "DT_DOUBLE") {
            std::vector<double> content;
            content.resize(content_size);
            request.AddFeed((*it)["name"], shape, content);
          } else if ((*it)["type"] == "DT_INT32") {
            std::vector<int> content;
            content.resize(content_size);
            request.AddFeed((*it)["name"], shape, content);
          } else if ((*it)["type"] == "DT_UINT8") {
            std::vector<unsigned char> content;
            content.resize(content_size);
            request.AddFeed((*it)["name"], shape, content);
          } else if ((*it)["type"] == "DT_INT8") {
            std::vector<signed char> content;
            content.resize(content_size);
            request.AddFeed((*it)["name"], shape, content);
          } else if ((*it)["type"] == "DT_INT64") {
            std::vector<long long> content;
            content.resize(content_size);
            request.AddFeed((*it)["name"], shape, content);
          } else if ((*it)["type"] == "DT_BOOL") {
            std::vector<bool> content;
            content.resize(content_size);
            request.AddFeed((*it)["name"], shape, content);
          } else if ((*it)["type"] == "DT_STRING") {
            std::vector<std::string> content;
            for (int i = 0; i < content_size; i++) {
              content.push_back("PACKAGE_611");
            }
            request.AddFeed((*it)["name"], shape, content);
          }
        }
      }
      loader->Predict(request, &response);
    } catch (std::exception &ex) {
    }
    std::cout << "Warm up successfully, start serving" << std::endl;
  }
  return loader;
}

int process(void *model_buf, const void *input_data, int input_size,
            void **output_data, int *output_size) {
  SavedModelLoader *loader = static_cast<SavedModelLoader *>(model_buf);
  if (input_size == 0) {
    std::string model_signature_info = loader->GetModelSignatureInfo();
    if (model_signature_info.empty()) {
      const char *errmsg = "input data should not be empty";
      *output_data = strndup(errmsg, strlen(errmsg));
      *output_size = strlen(errmsg);
      return 400;
    } else {
      *output_data =
          strndup(model_signature_info.c_str(), model_signature_info.length());
      *output_size = model_signature_info.length();
      return 200;
    }
  }

  PredictRequest request;
  PredictResponse response;
  std::string input_request = std::string((const char *)input_data, input_size);
  request.ParseFromString(input_request);
  int state = loader->Predict(request, &response);
  if (state < 0) {
    const char *errmsg = "Predict Process Failed";
    *output_data = strndup(errmsg, strlen(errmsg));
    *output_size = strlen(errmsg);
    return 500;
  }
  std::string output_response;
  response.SerializeToString(&output_response);
  *output_data = malloc(output_response.length());
  memcpy(*output_data, output_response.c_str(), output_response.length());
  *output_size = output_response.length();

  return 200;
}

int batch_process(void *model_buf, const void *input_data[], int *input_size,
                  void *output_data[], int *output_size) {
  SavedModelLoader *loader = static_cast<SavedModelLoader *>(model_buf);
  int cnt = 0;
  std::vector<long long> each_input_nums;
  each_input_nums.reserve(BUFSIZ);
  PredictRequest batch_request;
  PredictResponse batch_response;
  std::string first_input_str =
      std::string((const char *)*input_data, input_size[0]);
  batch_request.ParseFromString(first_input_str);
  for (const void **p = input_data; *p != NULL; p++, cnt++) {
    PredictRequest request;
    std::string input_str = std::string((const char *)*p, input_size[cnt]);
    request.ParseFromString(input_str);
    for (auto it = request.inputs().begin(); it != request.inputs().end();
         it++) {
      if ((it->second).array_shape().dim_size() > 0) {
        each_input_nums.push_back((it->second).array_shape().dim(0));
        break;
      }
    }
    if (cnt == 0) continue;
    auto iter = request.inputs().begin();
    for (; iter != request.inputs().end(); iter++) {
      if ((iter->second).array_shape().dim_size() <= 0) {
        continue;
      }
      auto proto = iter->second;
      auto batch_iter = batch_request.inputs().begin();
      for (; batch_iter != batch_request.inputs().end(); batch_iter++) {
        if (batch_iter->first == iter->first) {
          auto batch_proto = batch_iter->second;
          long long dim_size =
              proto.array_shape().dim(0) + batch_proto.array_shape().dim(0);
          batch_proto.mutable_array_shape()->set_dim(0, dim_size);
          for (int i = 0; i < proto.float_val_size(); i++)
            batch_proto.add_float_val(proto.float_val(i));
          for (int i = 0; i < proto.double_val_size(); i++)
            batch_proto.add_double_val(proto.double_val(i));
          for (int i = 0; i < proto.int_val_size(); i++)
            batch_proto.add_int_val(proto.int_val(i));
          for (int i = 0; i < proto.string_val_size(); i++)
            batch_proto.add_string_val(proto.string_val(i));
          for (int i = 0; i < proto.int64_val_size(); i++)
            batch_proto.add_int64_val(proto.int64_val(i));
          for (int i = 0; i < proto.bool_val_size(); i++)
            batch_proto.add_bool_val(proto.bool_val(i));
          (*batch_request.mutable_inputs())[iter->first] = batch_proto;
        }
      }
    }
  }
  int state = loader->Predict(batch_request, &batch_response);
  if (state < 0) {
    std::string errmsg = "Predict Process Failed";
    throw std::runtime_error(errmsg);
  }

  std::map<std::string, long long> output_content_pos;
  for (auto &res : batch_response.outputs()) output_content_pos[res.first] = 0;
  for (size_t i = 0; i < each_input_nums.size(); i++) {
    PredictResponse response;
    std::string output_result;
    auto batch_iter = batch_response.outputs().begin();
    for (; batch_iter != batch_response.outputs().end(); batch_iter++) {
      long long each_content_size = each_input_nums[i];
      auto batch_proto = batch_iter->second;
      tensorflow::eas::ArrayProto proto;
      proto.mutable_array_shape()->add_dim(each_content_size);
      for (int j = 1; j < batch_proto.array_shape().dim_size(); j++) {
        long long each_shape = batch_proto.array_shape().dim(j);
        proto.mutable_array_shape()->add_dim(each_shape);
        each_content_size *= each_shape;
      }
      proto.set_dtype(batch_proto.dtype());
      long long start_pos = output_content_pos[batch_iter->first];
      long long end_pos =
          output_content_pos[batch_iter->first] + each_content_size;
      for (long long j = start_pos; j < end_pos; j++) {
        if (batch_proto.float_val_size() > 0)
          proto.add_float_val(batch_proto.float_val(j));
        if (batch_proto.double_val_size() > 0)
          proto.add_double_val(batch_proto.double_val(j));
        if (batch_proto.int_val_size() > 0)
          proto.add_int_val(batch_proto.int_val(j));
        if (batch_proto.string_val_size() > 0)
          proto.add_string_val(batch_proto.string_val(j));
        if (batch_proto.int64_val_size() > 0)
          proto.add_int64_val(batch_proto.int64_val(j));
        if (batch_proto.bool_val_size() > 0)
          proto.add_bool_val(batch_proto.bool_val(j));
      }
      output_content_pos[batch_iter->first] = end_pos;
      (*response.mutable_outputs())[batch_iter->first] = proto;
    }
    response.SerializeToString(&output_result);
    void *output_response = malloc(output_result.length());
    memcpy(output_response, output_result.c_str(), output_result.length());
    output_data[i] = output_response;
    output_size[i] = output_result.length();
  }
  return 200;
}
}
