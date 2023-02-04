/**
 * An example of using Feed Forward Neural Network (FFN) for
 * solving Digit Recognizer problem from Kaggle website.
 *
 * The full description of a problem as well as datasets for training
 * and testing are available here https://www.kaggle.com/c/digit-recognizer
 *
 * mlpack is free software; you may redistribute it and/or modify it under the
 * terms of the 3-clause BSD license.  You should have received a copy of the
 * 3-clause BSD license along with mlpack.  If not, see
 * http://www.opensource.org/licenses/BSD-3-Clause for more information.
 *
 * @author Eugene Freyman
 * @author Omar Shrit
 */

#include <mlpack/mlpack.hpp>

using namespace mlpack;
using namespace std;

arma::Row<size_t> getLabels(arma::mat predOut)
{
  arma::Row<size_t> predLabels(predOut.n_cols);
  for (arma::uword i = 0; i < predOut.n_cols; ++i)
  {
    predLabels(i) = predOut.col(i).index_max();
  }
  return predLabels;
}

int main()
{
  // Dataset is randomly split into validation
  // and training parts in the following ratio.
  constexpr double RATIO = 0.1;
  // The number of neurons in the first layer.
  constexpr int H1 = 200;
  // The number of neurons in the second layer.
  constexpr int H2 = 100;
  // Step size of the optimizer.
  const double STEP_SIZE = 5e-3;
  // Number of data points in each iteration of SGD
  const size_t BATCH_SIZE = 64;
  // Allow up to 50 epochs, unless we are stopped early by EarlyStopAtMinLoss.
  const int EPOCHS = 50;

  // Labeled dataset that contains data for training is loaded from CSV file,
  // rows represent features, columns represent data points.
  arma::mat dataset;
  data::Load("../data/mnist_train.csv", dataset, true);

  // Originally on Kaggle dataset CSV file has header, so it's necessary to
  // get rid of the this row, in Armadillo representation it's the first column.
  arma::mat headerLessDataset =
      dataset.submat(0, 1, dataset.n_rows - 1, dataset.n_cols - 1);

  // Splitting the training dataset on training and validation parts.
  arma::mat train, valid;
  data::Split(headerLessDataset, train, valid, RATIO);

  // Getting training and validating dataset with features only and then
  // normalising
  const arma::mat trainX =
      train.submat(1, 0, train.n_rows - 1, train.n_cols - 1) / 255.0;
  const arma::mat validX =
      valid.submat(1, 0, valid.n_rows - 1, valid.n_cols - 1) / 255.0;

  // Labels should specify the class of a data point and be in the interval [0,
  // numClasses).

  // Creating labels for training and validating dataset.
  const arma::mat trainY = train.row(0);
  const arma::mat validY = valid.row(0);

  // Specifying the NN model. NegativeLogLikelihood is the output layer that
  // is used for classification problem. GlorotInitialization means that
  // initial weights in neurons are a uniform gaussian distribution.
  FFN<NegativeLogLikelihood, GlorotInitialization> model;
  // This is intermediate layer that is needed for connection between input
  // data and relu layer. Parameters specify the number of input features
  // and number of neurons in the next layer.
  model.Add<Linear>(H1);
  // The first relu layer.
  model.Add<ReLU>();
  // Intermediate layer between relu layers.
  model.Add<Linear>(H2);
  // The second relu layer.
  model.Add<ReLU>();
  // Dropout layer for regularization. First parameter is the probability of
  // setting a specific value to 0.
  model.Add<Dropout>(0.2);
  // Intermediate layer.
  model.Add<Linear>(10);
  // LogSoftMax layer is used together with NegativeLogLikelihood for mapping
  // output values to log of probabilities of being a specific class.
  model.Add<LogSoftMax>();

  cout << "Start training ..." << endl;

  // Set parameters for the Adam optimizer.
  ens::Adam optimizer(
      STEP_SIZE,  // Step size of the optimizer.
      BATCH_SIZE, // Batch size. Number of data points that are used in each
                  // iteration.
      0.9,        // Exponential decay rate for the first moment estimates.
      0.999, // Exponential decay rate for the weighted infinity norm estimates.
      1e-8,  // Value used to initialise the mean squared gradient parameter.
      EPOCHS * trainX.n_cols, // Max number of iterations.
      1e-8,           // Tolerance.
      true);

  // Declare callback to store best training weights.
  ens::StoreBestCoordinates<arma::mat> bestCoordinates;

  // Train neural network. If this is the first iteration, weights are
  // random, using current values as starting point otherwise.
  model.Train(trainX,
              trainY,
              optimizer,
              ens::PrintLoss(),
              ens::ProgressBar(),
              // Stop the training using Early Stop at min loss.
              ens::EarlyStopAtMinLoss(
                  [&](const arma::mat& /* param */)
                  {
                    double validationLoss = model.Evaluate(validX, validY);
                    cout << "Validation loss: " << validationLoss << "."
                        << endl;
                    return validationLoss;
                  }),
              // Store best coordinates (neural network weights)
              bestCoordinates);

  // Save the best training weights into the model.
  model.Parameters() = bestCoordinates.BestCoordinates();

  arma::mat predOut;
  // Getting predictions on training data points.
  model.Predict(trainX, predOut);
  // Calculating accuracy on training data points.
  arma::Row<size_t> predLabels = getLabels(predOut);
  double trainAccuracy =
      arma::accu(predLabels == trainY) / (double) trainY.n_elem * 100;
  // Getting predictions on validating data points.
  model.Predict(validX, predOut);
  // Calculating accuracy on validating data points.
  predLabels = getLabels(predOut);
  double validAccuracy =
      arma::accu(predLabels == validY) / (double) validY.n_elem * 100;

  cout << "Accuracy: train = " << trainAccuracy << "%,"
       << "\t valid = " << validAccuracy << "%" << endl;

  data::Save("model.bin", "model", model, false);

  // Loading test dataset (the one whose predicted labels
  // should be sent to kaggle website).
  data::Load("../data/mnist_test.csv", dataset, true);
  arma::mat testY = dataset.row(dataset.n_rows - 1);
  dataset.shed_row(dataset.n_rows - 1); // Strip labels before predicting.

  cout << "Predicting on test set..." << endl;
  arma::mat testPredOut;
  // Getting predictions on test data points.
  model.Predict(dataset, testPredOut);
  // Generating labels for the test dataset.
  arma::Row<size_t> testPred = getLabels(testPredOut);

  double testAccuracy = arma::accu(testPred == testY) /
      (double) testY.n_elem * 100;
  cout << "Accuracy: test = " << testAccuracy << "%" << endl;

  cout << "Saving predicted labels to \"results.csv\" ..." << endl;
  testPred.save("results.csv", arma::csv_ascii);

  cout << "Neural network model is saved to \"model.bin\"" << endl;
  cout << "Finished" << endl;
}