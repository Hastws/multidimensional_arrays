#include <chrono>

#include "data/data.h"
#include "exp/function.h"
#include "learning/module.h"
#include "learning/optimizer.h"
#include "multidimensional_arrays/multidimensional_arrays.h"

class MLP : public KD::Learning::Module {
 public:
  MLP(KD::Index in, KD::Index hidden_1, KD::Index out)
      : linear_1_(in, hidden_1), linear_2_(hidden_1, out) {}

  KD::MultidimensionalArrays Forward(
      const KD::MultidimensionalArrays &input) override {
    KD::MultidimensionalArrays x1 = linear_1_.Forward(input);
    KD::MultidimensionalArrays y = linear_2_.Forward(x1);
    return y;
  }

  KD::Learning::ParamsDict Parameters() override {
    return {{"linear_1", linear_1_.Parameters()},
            {"linear_2", linear_2_.Parameters()}};
  }

 private:
  KD::Learning::LinearWithReLU linear_1_;
  KD::Learning::Linear linear_2_;
};

int main() {
  constexpr KD::Index epoch = 3;
  constexpr KD::Index batch_size = 64;
  constexpr KD::BasicData lr = 0.05;
  constexpr KD::BasicData momentum = 0.90;
  constexpr KD::BasicData lr_decay_factor = 0.1;
  constexpr KD::Index lr_decay_epoch = 2;
  constexpr KD::Index print_iterators = 10;

  using namespace std::chrono;
  steady_clock::time_point start_tp = steady_clock::now();

  // dataset
  KD::SourceData::MNIST train_dataset(
      std::string(PROJECT_SOURCE_DIR) + "/data/train-images.idx3-ubyte",
      std::string(PROJECT_SOURCE_DIR) + "/data/train-labels.idx1-ubyte",
      batch_size, false);
  KD::SourceData::MNIST val_dataset(
      std::string(PROJECT_SOURCE_DIR) + "/data/t10k-images.idx3-ubyte",
      std::string(PROJECT_SOURCE_DIR) + "/data/t10k-labels.idx1-ubyte",
      batch_size, false);

  // model and criterion
  MLP mlp(KD::SourceData::MNIST::Img::n_pixels_, 64, 10);
  KD::Learning::CrossEntropy criterion;

  // optimizer
  KD::Learning::StochasticGradientDescentWithMomentum optimizer(
      mlp.Parameters(), lr, momentum);

  KD::Index n_samples;
  const KD::BasicData *batch_samples;
  const KD::Index *batch_labels;
  for (KD::Index i = 0; i < epoch; ++i) {
    LOG_INFO("Epoch " << i << " training...")
    LOG_INFO("total iterations: " << train_dataset.BatchesSize())
    train_dataset.Shuffle();

    if (i == lr_decay_epoch) {
      KD::BasicData optimizer_lr = optimizer.Lr();
      optimizer.SetLr(optimizer_lr * lr_decay_factor);
      LOG_INFO("Lr decay to " << optimizer.Lr())
    }

    for (KD::Index j = 0; j < train_dataset.BatchesSize(); ++j) {
      std::tie(n_samples, batch_samples, batch_labels) =
          train_dataset.GetBatch(j);
      KD::MultidimensionalArrays input(
          batch_samples, {n_samples, KD::SourceData::MNIST::Img::n_pixels_});

      KD::MultidimensionalArrays output = mlp.Forward(input);
      KD::MultidimensionalArrays loss = criterion.Forward(output, batch_labels);
      loss.Backward();

      optimizer.Step();
      optimizer.ZeroGrad();

      if (j % print_iterators == 0) {
        LOG_INFO("iter " << j << " | loss: " << loss.Item())
      }
    }

    LOG_INFO("Epoch " << i << " Evaluating...")
    KD::Index total_samples = 0, correct_samples = 0;
    for (KD::Index j = 0; j < val_dataset.BatchesSize(); ++j) {
      std::tie(n_samples, batch_samples, batch_labels) =
          val_dataset.GetBatch(j);
      KD::MultidimensionalArrays input(
          batch_samples, {n_samples, KD::SourceData::MNIST::Img::n_pixels_});
      KD::MultidimensionalArrays output = mlp.Forward(input);
      KD::MultidimensionalArrays predict =
          KD::Operator::CreateOperationArgmax(output, 1);

      for (KD::Index k = 0; k < n_samples; ++k) {
        ++total_samples;
        KD::Index pd_label = static_cast<KD::Index>(predict[{k}]);
        if (pd_label == batch_labels[k]) {
          ++correct_samples;
        }
      }
    }
    LOG_INFO("total samples: " << total_samples << " | correct samples: "
                               << correct_samples << " | acc: ")
    LOG_INFO(static_cast<KD::BasicData>(correct_samples) / total_samples)
  }

  steady_clock::time_point end_tp = steady_clock::now();
  duration<double> time_span =
      duration_cast<duration<double>>(end_tp - start_tp);
  LOG_INFO("Training finished. Training took " << time_span.count()
                                               << " seconds.")
  return 0;
}
