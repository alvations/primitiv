// Sample code to train/test the MNIST dataset:
//   http://yann.lecun.com/exdb/mnist/
//
// The model consists of a full-connected 2-layer (input/hidden/output)
// perceptron with the softmax cross entropy loss.
// In addition, his example calculates hidden/output layers using 2 different
// GPUs.
//
// Usage:
//   (set include/lib path correctly to use primitiv)
//   $ ./download_data.sh
//   $ g++ -std=c++11 ./mnist_multi_gpu.cc -lprimitiv
//   $ ./a.out

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <primitiv/primitiv.h>
#include <primitiv/primitiv_cuda.h>

using namespace std;
using namespace primitiv;
namespace F = primitiv::operators;
namespace I = primitiv::initializers;
namespace T = primitiv::trainers;

namespace {

const unsigned NUM_TRAIN_SAMPLES = 60000;
const unsigned NUM_TEST_SAMPLES = 10000;
const unsigned NUM_INPUT_UNITS = 28 * 28;
const unsigned NUM_HIDDEN_UNITS = 800;
const unsigned NUM_OUTPUT_UNITS = 10;
const unsigned BATCH_SIZE = 50;
const unsigned NUM_TRAIN_BATCHES = NUM_TRAIN_SAMPLES / BATCH_SIZE;
const unsigned NUM_TEST_BATCHES = NUM_TEST_SAMPLES / BATCH_SIZE;
const unsigned MAX_EPOCH = 100;

// Helper function to load input images.
vector<float> load_images(const string &filename, const unsigned n) {
  ifstream ifs(filename, ios::binary);
  if (!ifs.is_open()) {
    cerr << "File could not be opened: " << filename << endl;
    abort();
  }

  ifs.ignore(16);  // header
  const unsigned size = n * NUM_INPUT_UNITS;
  vector<unsigned char> buf(size);
  ifs.read(reinterpret_cast<char *>(&buf[0]), size);
  vector<float> ret(size);
  for (unsigned i = 0; i < size; ++i) ret[i] = buf[i] / 255.0;
  return ret;
}

// Helper function to load labels.
vector<char> load_labels(const string &filename, const unsigned n) {
  ifstream ifs(filename, ios::binary);
  if (!ifs.is_open()) {
    cerr << "File could not be opened: " << filename << endl;
    abort();
  }

  ifs.ignore(8);  // header
  vector<char> ret(n);
  ifs.read(&ret[0], n);
  return ret;
}

}  // namespace

int main() {
  // Loads data
  vector<float> train_inputs = ::load_images("data/train-images-idx3-ubyte", NUM_TRAIN_SAMPLES);
  vector<char> train_labels = ::load_labels("data/train-labels-idx1-ubyte", NUM_TRAIN_SAMPLES);
  vector<float> test_inputs = ::load_images("data/t10k-images-idx3-ubyte", NUM_TEST_SAMPLES);
  vector<char> test_labels = ::load_labels("data/t10k-labels-idx1-ubyte", NUM_TEST_SAMPLES);

  // Initializes 2 device objects which manage different GPUs.
  devices::CUDA dev0(0);  // GPU 0
  devices::CUDA dev1(1);  // GPU 1

  // Computation graph
  Graph g;
  Graph::set_default(g);

  // Parameters on GPU 0.
  Parameter pw1("w1", {NUM_HIDDEN_UNITS, NUM_INPUT_UNITS}, I::XavierUniform(), dev0);
  Parameter pb1("b1", {NUM_HIDDEN_UNITS}, I::Constant(0), dev0);
  
  // Parameters on GPU 1.
  Parameter pw2("w2", {NUM_OUTPUT_UNITS, NUM_HIDDEN_UNITS}, I::XavierUniform(), dev1);
  Parameter pb2("b2", {NUM_OUTPUT_UNITS}, I::Constant(0), dev1);

  // Trainer
  T::SGD trainer(.1);
  trainer.add_parameter(pw1);
  trainer.add_parameter(pb1);
  trainer.add_parameter(pw2);
  trainer.add_parameter(pb2);

  // Helper lambda to construct the predictor network.
  auto make_graph = [&](const vector<float> &inputs) {
    // We first store input values explicitly on GPU 0.
    Node x = F::input<Node>(Shape({NUM_INPUT_UNITS}, BATCH_SIZE), inputs, dev0);
    Node w1 = F::parameter<Node>(pw1);
    Node b1 = F::parameter<Node>(pb1);
    Node w2 = F::parameter<Node>(pw2);
    Node b2 = F::parameter<Node>(pb2);
    // The hidden layer is calculated and implicitly stored on GPU 0.
    Node h_on_gpu0 = F::relu(F::matmul(w1, x) + b1);
    // `copy()` transfers the hiddne layer to GPU 1.
    Node h_on_gpu1 = F::copy(h_on_gpu0, dev1);
    // The output layer is calculated and implicitly stored on GPU 1.
    return F::matmul(w2, h_on_gpu1) + b2;
    // Below line attempts to calculate values beyond multiple devices and
    // will throw an exception (try if it's OK with you).
    //return F::matmul(w2, h_on_gpu0) + b2;
  };

  // Batch randomizer
  mt19937 rng;
  vector<unsigned> ids(NUM_TRAIN_SAMPLES);
  iota(begin(ids), end(ids), 0);

  for (unsigned epoch = 0; epoch < MAX_EPOCH; ++epoch) {
    // Shuffles sample IDs.
    shuffle(begin(ids), end(ids), rng);

    // Training loop
    for (unsigned batch = 0; batch < NUM_TRAIN_BATCHES; ++batch) {
      // Makes a minibatch for training.
      vector<float> inputs(BATCH_SIZE * NUM_INPUT_UNITS);
      vector<unsigned> labels(BATCH_SIZE);
      for (unsigned i = 0; i < BATCH_SIZE; ++i) {
        const unsigned id = ids[i + batch * BATCH_SIZE];
        copy(&train_inputs[id * NUM_INPUT_UNITS],
             &train_inputs[(id + 1) * NUM_INPUT_UNITS],
             &inputs[i * NUM_INPUT_UNITS]);
        labels[i] = train_labels[id];
      }

      // Constructs the graph.
      g.clear();
      Node y = make_graph(inputs);
      Node loss = F::softmax_cross_entropy(y, labels, 0);
      Node avg_loss = F::batch::mean(loss);

      // Implicit forward, backward, and updates parameters.
      trainer.reset_gradients();
      g.backward(avg_loss);
      trainer.update();
    }

    unsigned match = 0;

    // Test loop
    for (unsigned batch = 0; batch < NUM_TEST_BATCHES; ++batch) {
      // Makes a test minibatch.
      vector<float> inputs(BATCH_SIZE * NUM_INPUT_UNITS);
      copy(&test_inputs[batch * BATCH_SIZE * NUM_INPUT_UNITS],
           &test_inputs[(batch + 1) * BATCH_SIZE * NUM_INPUT_UNITS],
           &inputs[0]);

      // Constructs the graph.
      g.clear();
      Node y = make_graph(inputs);

      // Gets outputs, argmax, and compares them with the label.
      vector<float> y_val = g.forward(y).to_vector();
      for (unsigned i = 0; i < BATCH_SIZE; ++i) {
        float maxval = -1e10;
        unsigned argmax = -1;
        for (unsigned j = 0; j < NUM_OUTPUT_UNITS; ++j) {
          float v = y_val[j + i * NUM_OUTPUT_UNITS];
          if (v > maxval) maxval = v, argmax = j;
        }
        if (argmax == test_labels[i + batch * BATCH_SIZE]) ++match;
      }
    }

    const float accuracy = 100.0 * match / NUM_TEST_SAMPLES;
    printf("epoch %d: accuracy: %.2f%%\n", epoch, accuracy);
  }

  return 0;
}
