#include <algorithm>

#include "Buffer.h"
#include "Generator.h"
#include "BoundaryConditions.h"
#include "ThroughputPredictorLoader.h"
#include "Type.h"

namespace {
  using namespace Halide;

  class ThroughputPredictorPipeline {
    public:

      ImageParam pipeline_features{Float(32), 4, "pipeline_features"};
      ImageParam schedule_features{Float(32), 3, "schedule_features"};

      Stats feature_stats;
       /**
      Buffer<float> pipeline_std{56,7};
      Buffer<float> pipeline_mean{56,7};
      **/
      Buffer<float> schedule_std{18};
      Buffer<float> schedule_mean{18};

      Buffer<float> head1_filter{20,56,7};
      Buffer<float> head1_bias{20};
      
      Buffer<float> head2_filter{20,18};
      Buffer<float> head2_bias{20};

      Buffer<float> filter1{40,40,3};
      Buffer<float> bias1{40};
      Buffer<float> filter2{40,40,3};
      Buffer<float> bias2{40};
      Buffer<float> filter3{80,40,3};
      Buffer<float> bias3{80};
      Buffer<float> filter4{120,80,3};
      Buffer<float> bias4{120};
      Buffer<float> filter5{160,120,3};
      Buffer<float> bias5{160};

      Buffer<float> fc1_weights{80,160};
      Buffer<float> fc1_bias{80};
      Buffer<float> fc2_weights{40,80};
      Buffer<float> fc2_bias{40};
      Buffer<float> fc3_weights{1,40};
      Buffer<float> fc3_bias{1};

			Func padded_pipeline_features;
      Func padded_schedule_features;
      
      Func f_head1_conv, f_head2_conv;
      Func f_head1_relu, f_head2_relu;
      Func f_conv1_stage1, f_conv1_stage2, f_conv2, f_conv3, f_conv4, f_conv5;
			Func f_ReLU1, f_ReLU2, f_ReLU3, f_ReLU4, f_ReLU5, f_ReLU6, f_ReLU7;
			Func f_pool3, f_pool4;
			Func f_reduce, f_fc1, f_fc2, prediction;

      ThroughputPredictorPipeline(Weights weights, Stats stats) : 
                                               feature_stats(stats),
                                               head1_filter(weights.head1_filter), head1_bias(weights.head1_bias),
                                               head2_filter(weights.head2_filter), head2_bias(weights.head2_bias),
                                               filter1(weights.conv1_filter), bias1(weights.conv1_bias),
                                               filter2(weights.conv2_filter), bias2(weights.conv2_bias),
                                               filter3(weights.conv3_filter), bias3(weights.conv3_bias),
                                               filter4(weights.conv4_filter), bias4(weights.conv4_bias),
                                               filter5(weights.conv5_filter), bias5(weights.conv5_bias),
                                               fc1_weights(weights.fc1_filter), fc1_bias(weights.fc1_bias),
                                               fc2_weights(weights.fc2_filter), fc2_bias(weights.fc2_bias),
                                               fc3_weights(weights.fc3_filter), fc3_bias(weights.fc3_bias) {
        
        
        Var c("c"), w("w"), n("n");
        padded_pipeline_features = Halide::BoundaryConditions::constant_exterior(pipeline_features, 0);	
        padded_schedule_features = Halide::BoundaryConditions::constant_exterior(schedule_features, 0);
        
        RDom r_head1(head1_filter.dim(1).min(), head1_filter.dim(1).extent(),
                     head1_filter.dim(2).min(), head1_filter.dim(2).extent());

        RDom r_head2(head2_filter.dim(1).min(), head2_filter.dim(1).extent());

        RDom r1_stage1(head1_filter.dim(0).min(), head1_filter.dim(0).extent(),
                       filter1.dim(2).min(), filter1.dim(2).extent());

        RDom r1_stage2(head2_filter.dim(0).min(), head2_filter.dim(0).extent(),
                       filter1.dim(2).min(), filter1.dim(2).extent());

        RDom r2(filter2.dim(1).min(), filter2.dim(1).extent(),
                filter2.dim(2).min(), filter2.dim(2).extent());

        RDom r3(filter3.dim(1).min(), filter3.dim(1).extent(),
                filter3.dim(2).min(), filter3.dim(2).extent());

        RDom r4(filter4.dim(1).min(), filter4.dim(1).extent(),
                filter4.dim(2).min(), filter4.dim(2).extent());

        RDom r5(filter5.dim(1).min(), filter5.dim(1).extent(),
                filter5.dim(2).min(), filter5.dim(2).extent());

        /* Assuming input features are given zero padded to the longest pipeline within the batch of size n */
        //RDom r_reduce(0, (feat_len/num_features) / 4);
        RDom r_reduce(0, (Halide::max(22, schedule_features.dim(2).extent())-16)/ 4);


        f_head1_conv(n, c, w) = head1_bias(c);
        f_head1_conv(n, c, w) += head1_filter(c, r_head1.x, r_head1.y) * padded_pipeline_features(n, r_head1.x, r_head1.y, w);
        f_head1_relu(n, c, w) = max(0, f_head1_conv(n, c, w));

        f_head2_conv(n, c, w) = head2_bias(c);
        f_head2_conv(n, c, w) += head2_filter(0, r_head2) * padded_schedule_features(n, r_head2, w);
        f_head2_relu(n, c, w) = max(0, f_head2_conv(n, c, w));
        
        // network trunk
        // first 30 channel are from head1_conv output, next 20 input channels are from head2_conv output
        // have to do two stagees for conv1 to convolve over each head's outputs

        f_conv1_stage1(n,c,w) = bias1(c);
        f_conv1_stage1(n, c, w) += filter1(c, r1_stage1.x, r1_stage1.y) * f_head1_relu(n, r1_stage1.x, w+r1_stage1.y);
        
        f_conv1_stage2(n, c, w) = f_conv1_stage1(n, c, w);
        f_conv1_stage2(n, c, w) += filter1(c, head1_filter.dim(0).extent()+r1_stage2.x, r1_stage2.y) * f_head2_relu(n, r1_stage2.x, w+r1_stage2.y);
        f_ReLU1(n, c, w) = max(0, f_conv1_stage2(n, c, w));

        f_conv2(n, c, w) = bias2(c);;
        f_conv2(n, c, w) += filter2(c, r2.x, r2.y) * f_ReLU1(n, r2.x, w+r2.y);
        f_ReLU2(n, c, w) = max(0, f_conv2(n, c, w));

        f_conv3(n, c, w) = bias3(c);
        f_conv3(n, c, w) += filter3(c, r3.x, r3.y) + f_ReLU2(n, r3.x, w+r3.y);
        f_ReLU3(n, c, w) = max(0, f_conv3(n, c, w));
     
        f_pool3(n, c, w) = 0.5f * (f_ReLU3(n, c, w*2) + f_ReLU3(n, c, w*2+1)); 

        f_conv4(n, c, w) = bias4(c);;
        f_conv4(n, c, w) += filter4(c, r4.x, r4.y) * f_pool3(n, r4.x, w+r4.y);
        f_ReLU4(n, c, w) = max(0, f_conv4(n, c, w));
        
        f_pool4(n, c, w) = 0.5f * (f_ReLU4(n, c, w*2) + f_ReLU4(n, c, w*2+1)); 
        
        f_conv5(n, c, w) = bias5(c);;
        f_conv5(n, c, w) += filter5(c, r5.x, r5.y) * f_pool4(n, r5.x, w+r5.y);
        f_ReLU5(n, c, w) = max(0, f_conv5(n, c, w));

        f_reduce(n, c, w) = 0.0f;
        f_reduce(n, c, w) += f_ReLU5(n, c, w + r_reduce);

        RDom r_fc1(fc1_weights.dim(1).min(), fc1_weights.dim(1).extent());
        RDom r_fc2(fc2_weights.dim(1).min(), fc2_weights.dim(1).extent());
        RDom r_fc3(fc3_weights.dim(1).min(), fc3_weights.dim(1).extent());
        
        f_fc1(n, c, w) = fc1_bias(c);;
        f_fc1(n, c, w) += f_reduce(n, r_fc1, w) * fc1_weights(c, r_fc1);
        f_ReLU6(n, c, w) = max(0, f_fc1(n, c, w));

        
        f_fc2(n, c, w) = fc2_bias(c);;
        f_fc2(n, c, w) += f_ReLU6(n, r_fc2, w) * fc2_weights(c, r_fc2);
        f_ReLU7(n, c, w) = max(0, f_fc2(n, c, w));

        prediction(n, c, w) = fc3_bias(c);;
        prediction(n, c, w) += f_ReLU7(n, r_fc3, w) * fc3_weights(c, r_fc3);

        f_head1_conv.compute_root();
        f_head1_relu.compute_root();
        f_head2_conv.compute_root();
        f_head2_relu.compute_root();

        f_conv1_stage1.compute_root();
        f_conv1_stage2.compute_root();
        f_ReLU1.compute_root();
        
        f_conv2.compute_root();
        f_ReLU2.compute_root();
        
        f_conv3.compute_root();
        f_ReLU3.compute_root();
        
        f_pool3.compute_root();
        
        f_conv4.compute_root();
        f_ReLU4.compute_root();
        
        f_pool4.compute_root();
        
        f_conv5.compute_root();
        f_ReLU5.compute_root();
        
        f_reduce.compute_root();
        
        f_fc1.compute_root();
        f_ReLU6.compute_root();
        f_fc2.compute_root();
        f_ReLU7.compute_root();
        prediction.compute_root();

        prediction.compile_jit(); 
        std::cout << "JIT COMPILED PIPELINE" << std::endl; 
      }
    
      void set_inputs(Buffer<float> pipeline_feats, Buffer<float> schedule_feats) {
        pipeline_features.set(pipeline_feats);
        schedule_features.set(schedule_feats);
      }

  };
};
