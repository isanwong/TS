/* Copyright (C) 2013 Ion Torrent Systems, Inc. All Rights Reserved */

#include "SigmaGenerator.h"




void BasicSigmaGenerator::GenerateSigmaByRegression(vector<float> &prediction, vector<int> &test_flow, vector<float> &sigma_estimate){
     // use latent variable to predict sigma by predicted signal
  for (unsigned int t_flow=0; t_flow<test_flow.size(); t_flow++){
     int j_flow = test_flow[t_flow];
     
     sigma_estimate[j_flow] = InterpolateSigma(prediction[j_flow]);  // it's a prediction! always positive
     //cout << "sigma " << prediction[j_flow] << "\t" << sigma_estimate[j_flow] << endl;
  }
}

void BasicSigmaGenerator::GenerateSigma(CrossHypotheses &my_cross){
   for (unsigned int i_hyp=0; i_hyp<my_cross.residuals.size(); i_hyp++){
      GenerateSigmaByRegression(my_cross.predictions[i_hyp], my_cross.test_flow, my_cross.sigma_estimate[i_hyp]);
   }
}

void BasicSigmaGenerator::ResetUpdate(){

   SimplePrior(); // make sure we have some stability
}

void BasicSigmaGenerator::ZeroAccumulator(){
    for (unsigned int i_level = 0; i_level<accumulated_sigma.size(); i_level++){
      accumulated_sigma[i_level] = 0.0f;
      accumulated_weight[i_level] = 0.0f;
    }
}

void BasicSigmaGenerator::SimplePrior(){
   // sum r^2
   ZeroAccumulator();
   float basic_weight = prior_weight; // prior strength
   for (unsigned int i_level = 0; i_level<accumulated_sigma.size(); i_level++)
   {
      // expected variance per level
      // this is fairly arbitrary as we expect the data to overcome our weak prior here
      float square_level = i_level*i_level+1.0f;  // avoid zero division
      // approximate quadratic increase in sigma- should be linear, but empirically we see more than expected
      float sigma_square =  prior_sigma_regression[0]+prior_sigma_regression[1]*square_level;
      sigma_square *=sigma_square; // push squared value
      PushLatent(basic_weight, (float) i_level, sigma_square);
   }
   DoLatentUpdate();  // the trivial model for sigma by intensity done
}

// retrieve latent
float BasicSigmaGenerator::InterpolateSigma(float x_val){
    float t_val = max(x_val, 0.0f);
    int low_level = (int) t_val; // floor cast because x_val positive
    int hi_level = low_level+1;
    if (low_level>max_level){
       low_level = hi_level = max_level;
       t_val = (float) low_level;
    }    
    if (hi_level>max_level) hi_level = max_level;
    float delta_low = t_val - low_level;
    float delta_hi = 1.0f-delta_low;
    return(latent_sigma[low_level]*delta_hi + latent_sigma[hi_level]*delta_low);
}

void BasicSigmaGenerator::PushLatent(float responsibility, float x_val, float y_val){
    // interpolation and add
    float t_val = max(x_val, 0.0f);
    int low_level = (int) t_val; // floor cast because x_val non-negative
    int hi_level = low_level+1;
    if (low_level>max_level){
       low_level = hi_level = max_level;
       t_val = (float) low_level;
    }
    if (hi_level>max_level) hi_level = max_level;
    float delta_low = t_val - low_level;
    float delta_hi = 1.0f-delta_low;
    accumulated_sigma[low_level] += responsibility*delta_hi*y_val;
    accumulated_weight[low_level] += responsibility*delta_hi;
    accumulated_sigma[hi_level] += responsibility*delta_low*y_val;
    accumulated_weight[hi_level] += responsibility*delta_low;
}

void BasicSigmaGenerator::AddOneUpdateForHypothesis(vector<float> &prediction, float responsibility, float skew_estimate, vector<int> &test_flow, vector<float> &residuals){
  for (unsigned int t_flow=0; t_flow<test_flow.size(); t_flow++){
     int j_flow = test_flow[t_flow];
     float y_val =residuals[j_flow]*residuals[j_flow]; 
     // handle skew
     // note that this is >opposite< t-dist formula
     if (residuals[j_flow]>0)
       y_val = y_val/(skew_estimate*skew_estimate);
     else
       y_val = y_val * skew_estimate*skew_estimate;
     
     float x_val = prediction[j_flow];
     PushLatent(responsibility,x_val,y_val);
  }
}

void BasicSigmaGenerator::AddCrossUpdate(CrossHypotheses &my_cross){
   for (unsigned int i_hyp=1; i_hyp<my_cross.residuals.size(); i_hyp++){  // no outlier values count here
      AddOneUpdateForHypothesis(my_cross.predictions[i_hyp], my_cross.responsibility[i_hyp], my_cross.skew_estimate, my_cross.test_flow, my_cross.residuals[i_hyp]);
   }
}

void BasicSigmaGenerator::AddNullUpdate(CrossHypotheses &my_cross){
  unsigned int i_hyp =0;
  
  vector<int> all_flows;
  all_flows.assign(my_cross.delta.size(), 0.0f);
  for (unsigned int i_flow=0; i_flow<all_flows.size(); i_flow++)
    all_flows[i_flow] = i_flow;
  AddOneUpdateForHypothesis(my_cross.predictions[i_hyp], 1.0f, 1.0f, my_cross.test_flow, my_cross.residuals[i_hyp]);

}

void BasicSigmaGenerator::DoLatentUpdate(){

   for (unsigned int i_level=0; i_level<latent_sigma.size(); i_level++){
      latent_sigma[i_level] = sqrt(accumulated_sigma[i_level]/accumulated_weight[i_level]);  
      //cout << latent_sigma[i_level] << "\t" << i_level << endl;
   }
}

void BasicSigmaGenerator::PushToPrior(){
  prior_latent_sigma = latent_sigma;
};

void BasicSigmaGenerator::PopFromLatentPrior(){
  ZeroAccumulator();
  accumulated_sigma = prior_latent_sigma;
  accumulated_weight.assign(accumulated_sigma.size(), 1.0f);
};


// generate regression using all positions
// good for initial estimate for low intensity reads
void BasicSigmaGenerator::NullUpdateSigmaGenerator(ShortStack &total_theory) {
// put everything to null
  ResetUpdate();

  //for (unsigned int i_read=0; i_read<total_theory.my_hypotheses.size(); i_read++){
  for (unsigned int i_ndx = 0; i_ndx < total_theory.valid_indexes.size(); i_ndx++) {
    unsigned int i_read = total_theory.valid_indexes[i_ndx];
    AddNullUpdate(total_theory.my_hypotheses[i_read]);
  }
  DoLatentUpdate();  // new latent predictors for sigma
  PushToPrior();
}

// important: residuals do not need to be reset before this operation (predictions have been corrected for bias already)
void BasicSigmaGenerator::UpdateSigmaGenerator(ShortStack &total_theory) {
// put everything to null
  ResetUpdate();

  //for (unsigned int i_read=0; i_read<total_theory.my_hypotheses.size(); i_read++){
  for (unsigned int i_ndx = 0; i_ndx < total_theory.valid_indexes.size(); i_ndx++) {
    unsigned int i_read = total_theory.valid_indexes[i_ndx];
    AddCrossUpdate(total_theory.my_hypotheses[i_read]);
  }
  DoLatentUpdate();  // new latent predictors for sigma
}

void BasicSigmaGenerator::UpdateSigmaEstimates(ShortStack &total_theory) {
  //for (unsigned int i_read=0; i_read<total_theory.my_hypotheses.size(); i_read++){
  for (unsigned int i_ndx = 0; i_ndx < total_theory.valid_indexes.size(); i_ndx++) {
    unsigned int i_read = total_theory.valid_indexes[i_ndx];
    GenerateSigma(total_theory.my_hypotheses[i_read]);
  }
}

void BasicSigmaGenerator::DoStepForSigma(ShortStack &total_theory) {

  UpdateSigmaGenerator(total_theory);
  UpdateSigmaEstimates(total_theory);
  total_theory.UpdateRelevantLikelihoods();
}